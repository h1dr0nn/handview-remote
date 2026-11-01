#pragma once
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <stdexcept>
#include <cstring>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <deque>
#include <codecapi.h>
#include <strmif.h>
#include "handview/protocol/messages.hpp"
class H264Decoder {
    template<class T> using Com = Microsoft::WRL::ComPtr<T>;
    Com<IMFTransform> decoder;
    bool com=false, mf=false;
    void check(HRESULT hr) {
        if(FAILED(hr))throw std::runtime_error("Media Foundation decoder HRESULT="+std::to_string(static_cast<unsigned long>(hr)));
    }
    void output_type() {
        for(DWORD i=0;;++i) {
            Com<IMFMediaType> type;
            auto hr=decoder->GetOutputAvailableType(0,i,&type);
            check(hr);
            GUID subtype {};
            check(type->GetGUID(MF_MT_SUBTYPE,&subtype));
            if(subtype==MFVideoFormat_NV12) {
                check(decoder->SetOutputType(0,type.Get(),0));
                return;
            }
        }
    }
    public:
    std::deque<std::uint32_t> pending;
    H264Decoder(std::uint16_t width,std::uint16_t height) {
        try {
            check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
            com=true;
            check(MFStartup(MF_VERSION));
            mf=true;
            check(CoCreateInstance(CLSID_CMSH264DecoderMFT,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&decoder)));
            Com<IMFAttributes> attributes;
            if(SUCCEEDED(decoder->GetAttributes(&attributes))) check(attributes->SetUINT32(MF_LOW_LATENCY,TRUE));
            Com<ICodecAPI> codec;
            if(SUCCEEDED(decoder.As(&codec))) {
                VARIANT value;
                VariantInit(&value);
                value.vt=VT_BOOL;
                value.boolVal=VARIANT_TRUE;
                codec->SetValue(&CODECAPI_AVLowLatencyMode,&value);
            }
            Com<IMFMediaType> type;
            check(MFCreateMediaType(&type));
            check(type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video));
            check(type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264));
            check(MFSetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,width,height));
            check(MFSetAttributeRatio(type.Get(),MF_MT_FRAME_RATE,60,1));
            check(decoder->SetInputType(0,type.Get(),0));
            output_type();
            check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,0));
            check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,0));
        }
        catch(...) {
            decoder.Reset();
            if(mf)MFShutdown();
            if(com)CoUninitialize();
            throw;
        }
    }
    ~H264Decoder() {
        decoder.Reset();
        if(mf)MFShutdown();
        if(com)CoUninitialize();
    }
    std::vector<std::uint32_t> drain() {
        std::vector<std::uint32_t> result;
        for(;;) {
            MFT_OUTPUT_STREAM_INFO info {};
            check(decoder->GetOutputStreamInfo(0,&info));
            Com<IMFSample> sample;
            Com<IMFMediaBuffer> buffer;
            MFT_OUTPUT_DATA_BUFFER out {};
            if(!(info.dwFlags&MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
                check(MFCreateSample(&sample));
                check(MFCreateMemoryBuffer(info.cbSize,&buffer));
                check(sample->AddBuffer(buffer.Get()));
                out.pSample=sample.Get();
            }
            DWORD status=0;
            auto hr=decoder->ProcessOutput(0,1,&out,&status);
            if(out.pEvents)out.pEvents->Release();
            if(out.pSample && out.pSample!=sample.Get())out.pSample->Release();
            if(hr==MF_E_TRANSFORM_NEED_MORE_INPUT)break;
            if(hr==MF_E_TRANSFORM_STREAM_CHANGE) {
                output_type();
                continue;
            }
            check(hr);
            if(pending.empty())throw std::runtime_error("Decoder produced an untracked frame");
            result.push_back(pending.front());
            pending.pop_front();
        }
        return result;
    }
    std::vector<std::uint32_t> feed(const handview::protocol::VideoFrame& frame) {
        Com<IMFSample> sample;
        Com<IMFMediaBuffer> buffer;
        check(MFCreateSample(&sample));
        check(MFCreateMemoryBuffer(static_cast<DWORD>(frame.data.size()),&buffer));
        BYTE* data=nullptr;
        check(buffer->Lock(&data,nullptr,nullptr));
        memcpy(data,frame.data.data(),frame.data.size());
        check(buffer->Unlock());
        check(buffer->SetCurrentLength(static_cast<DWORD>(frame.data.size())));
        check(sample->AddBuffer(buffer.Get()));
        check(sample->SetSampleTime(static_cast<LONGLONG>(frame.ptsUs*10)));
        check(sample->SetSampleDuration(166667));
        check(decoder->ProcessInput(0,sample.Get(),0));
        pending.push_back(frame.frameSeq);
        return drain();
    }
    std::vector<std::uint32_t> finish() {
        check(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM,0));
        check(decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN,0));
        return drain();
    }
};

#pragma once
#include "handview/net/net.hpp"
#include <charconv>
#include <map>
#include <stdexcept>
#include <string>
#include <iostream>
namespace testtool {
    struct Args {
        std::map<std::string,std::string> values;
        Args(int argc,char** argv, const std::string& flags) {
            for(int i=1;i<argc;++i) {
                std::string key=argv[i];
                if(key.rfind("--",0)!=0 || values.contains(key)) throw std::runtime_error("Invalid or duplicate argument: "+key);
                if(flags.find("|"+key+"|")!=std::string::npos) values[key]="1";
                else {
                    if(i+1==argc || std::string(argv[i+1]).rfind("--",0)==0) throw std::runtime_error("Missing value: "+key);
                    values[key]=argv[++i];
                }
            }
        }
        void check(const std::string& allowed) const {
            for(const auto& [k,v]:values) {
                (void)v;
                if(allowed.find("|"+k+"|")==std::string::npos) throw std::runtime_error("Unknown argument: "+k);
            }
        }
        bool has(const std::string& k) const {
            return values.contains(k);
        }
        std::string get(const std::string& k,std::string fallback="") const {
            auto i=values.find(k);
            return i==values.end()?fallback:i->second;
        }
    };
    inline unsigned number(const std::string& s,unsigned low,unsigned high) {
        unsigned n=0;
        auto r=std::from_chars(s.data(),s.data()+s.size(),n);
        if(r.ec!=std::errc {}
        ||r.ptr!=s.data()+s.size()||n<low||n>high)throw std::runtime_error("Invalid number: "+s);
        return n;
    }
    inline std::pair<std::uint16_t,std::uint16_t> size(const std::string& s) {
        auto p=s.find('x');
        if(p==s.npos)throw std::runtime_error("Expected WxH");
        return {static_cast<std::uint16_t>(number(s.substr(0,p),2,8192)),static_cast<std::uint16_t>(number(s.substr(p+1),2,8192))};
    }
    inline std::array<std::uint8_t,32> token(const std::string& s) {
        if(s.size()!=64)throw std::runtime_error("Token must contain 64 hex characters");
        std::array<std::uint8_t,32> t {};
        for(size_t i=0;i<32;++i) {
            unsigned v=0;
            auto r=std::from_chars(s.data()+2*i,s.data()+2*i+2,v,16);
            if(r.ec!=std::errc {}
            ||r.ptr!=s.data()+2*i+2)throw std::runtime_error("Invalid token hex");
            t[i]=static_cast<std::uint8_t>(v);
        }
        return t;
    }
}

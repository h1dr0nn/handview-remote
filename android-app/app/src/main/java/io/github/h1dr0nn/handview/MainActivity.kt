package io.github.h1dr0nn.handview

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import io.github.h1dr0nn.handview.protocol.PROTOCOL_VERSION

// singleTop: the host relaunches this activity with token + port extras.
// Session handling (onCreate / onNewIntent) lands in M2.
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    UsbWaitingPlaceholder()
                }
            }
        }
    }
}

@Composable
private fun UsbWaitingPlaceholder() {
    Column(
        modifier = Modifier.fillMaxSize().padding(24.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(stringResource(R.string.usb_waiting_title), style = MaterialTheme.typography.titleLarge)
        Text("Protocol v$PROTOCOL_VERSION", style = MaterialTheme.typography.bodySmall)
    }
}

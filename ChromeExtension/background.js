let ws = null;
let reconnectTimer = null;
const WS_URL = "ws://127.0.0.1:3001";
const HEALTH_ALARM = "media-ws-health";

function clearReconnectTimer() {
  if (reconnectTimer !== null) {
    clearTimeout(reconnectTimer);
    reconnectTimer = null;
  }
}

function scheduleReconnect() {
  clearReconnectTimer();
  reconnectTimer = setTimeout(connectWebSocket, 1000);
}

function sendCommandToYouTubeTabs(command) {
  chrome.tabs.query({ url: ["*://youtube.com/*", "*://*.youtube.com/*"] }, (tabs) => {
    for (const tab of tabs) {
      if (tab.id === undefined) continue;

      chrome.scripting.executeScript(
        {
          target: { tabId: tab.id },
          args: [command],
          func: (receivedCommand) => {
            const video = document.querySelector("video") || document.querySelector("video.html5-main-video");
            if (!video) {
              return;
            }

            switch (receivedCommand) {
              case "forward":
                video.currentTime += 10;
                video.dispatchEvent(new Event("seeking"));
                break;
              case "back":
                video.currentTime -= 10;
                video.dispatchEvent(new Event("seeking"));
                break;
              case "playpause":
                if (video.paused) {
                  video.play();
                } else {
                  video.pause();
                }
                break;
              default:
                break;
            }
          }
        },
        () => {
          if (chrome.runtime.lastError) {
            console.debug("[WS] executeScript skipped:", chrome.runtime.lastError.message);
          }
        }
      );
    }
  });
}

function connectWebSocket() {
  if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
    return;
  }

  clearReconnectTimer();

  ws = new WebSocket(WS_URL);

  ws.onopen = () => {
    console.log("[WS] connected");
  };

  ws.onmessage = (event) => {
    const command = String(event.data || "").trim();
    if (command.length > 0) {
      sendCommandToYouTubeTabs(command);
    }
  };

  ws.onerror = () => {
    if (ws !== null) {
      ws.close();
    }
  };

  ws.onclose = () => {
    console.log("[WS] disconnected");
    ws = null;
    scheduleReconnect();
  };
}

chrome.runtime.onStartup.addListener(() => {
  connectWebSocket();
});

chrome.runtime.onInstalled.addListener(() => {
  connectWebSocket();
});

chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name !== HEALTH_ALARM) {
    return;
  }

  if (!ws || ws.readyState !== WebSocket.OPEN) {
    connectWebSocket();
  }
});

chrome.alarms.create(HEALTH_ALARM, { periodInMinutes: 1 });
connectWebSocket();

function getVideoElement() {
  return document.querySelector("video") || document.querySelector("video.html5-main-video");
}

function runWithVideo(command, retryCount = 0) {
  const video = getVideoElement();
  if (!video) {
    if (retryCount < 10) {
      setTimeout(() => runWithVideo(command, retryCount + 1), 100);
    }
    return;
  }

  switch (command) {
    case "forward":
      handleForward(video);
      break;
    case "back":
      handleBack(video);
      break;
    case "playpause":
      handlePlayPause(video);
      break;
    default:
      break;
  }
}

function handleForward(video) {
  video.currentTime += 10;
  video.dispatchEvent(new Event("seeking"));
}

function handleBack(video) {
  video.currentTime -= 10;
  video.dispatchEvent(new Event("seeking"));
}

function handlePlayPause(video) {
  if (video.paused) {
    video.play();
  } else {
    video.pause();
  }
}

chrome.runtime.onMessage.addListener((message) => {
  if (!message || message.type !== "media-command") {
    return;
  }

  runWithVideo(message.command);
});

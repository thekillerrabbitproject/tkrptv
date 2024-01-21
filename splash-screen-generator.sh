gst-launch-1.0 videotestsrc pattern=smpte100 ! \
  video/x-raw,width=720,height=576 ! \
  textoverlay text="killerrabb.it" valignment=top font-desc="VCR OSD Mono, 32" ypad=200 ! \
  textoverlay text="presents" valignment=top font-desc="VCR OSD Mono, 22" ypad=240 ! \
  textoverlay text="TKRP TV" valignment=top font-desc="VCR OSD Mono, 36" ypad=294 ! \
  textoverlay text="Loading" valignment=bottom halignment=left font-desc="VCR OSD Mono, 18" ! \
  pngenc ! filesink location=splash.png

# GST_WordRemove
With this code, you can remove the first recorded word with every signal

It uses ALSA to get the recording device.

After you send a 36 signal to the process, it'll start recording. If 5 seconds has passed and no samples has been caught, it'll simply stop.

You gotta add as an argument to the process the RMS value which will be your threshold. That means that if you speak lower than the threshold, the program will not save any samples of that.

In status, you tell the program where you want it to save the audio file.

It's adviced to change the device property of alsasrc to one that fits you the most.

If another audio format is needed, you can convert it after the recording or you can change the elements "flacenc" to any encoder you need.

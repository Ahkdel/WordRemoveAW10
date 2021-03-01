# GST_WordRemove
With this code, you can remove the first recorded word with every signal

It uses ALSA to get the recording device.

After you send a 36 signal to the process, it'll start recording. If 5 seconds has passed and no samples has been caught, it'll simply stop.

You gotta add as an argument to the process the RMS value which will be your threshold. That means that if you speak lower than the threshold, the program will not save any samples of that.

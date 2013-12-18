microbridge-arduino
===================

This library is a modified version of MicroBridge downloaded from  http://code.google.com/p/microbridge/. 

1. Minor modification is done to correctly configure SPI port of Seeeduino ADK Main Board. 
This modification can also be used for any Any Android ADK Reference board

2. A new sketch SeeeduinoADKDemo.pde is added to showcase Seeeduino ADK Main Board.

3. Added fix for Android version >= 4.2 where an RSA key is required to connect to and use the ADB port. Fix was not discovered by me, but documented here https://code.google.com/p/microbridge/issues/detail?id=21
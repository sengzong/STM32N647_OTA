# GT9xxx touch controller

The existing board driver uses software I2C on PD4/PD14, reset PD10, and interrupt
PB3. Initialization now logs the four raw product ID bytes to help distinguish
controller communication failures from UI hit-testing failures.

**Now work in xorg-1.19!!!**

This version based on Archers yaourt sisimedia-0.10.3 pathed for use with xorg-server version > 1.12
adapted by rasdark (andrew.senik@gmail.com)

For xorg-1.12 just clone and:
```
git checkout for-xorg-1.12-1
```

For xorg-1.18 just clone and:
```
git checkout for-xorg-1.18
```

For xorg-1.19 just clone and:
```
git checkout for-xorg-1.19
```

in my gentoo box work with:
```
./configure --prefix=/usr --disable-static && make && make install
```

sample:
```
$ inxi -G
Graphics:  Card: Silicon Integrated Systems [SiS] 771/671 PCIE VGA Display Adapter
           Display Server: x11 (X.Org 1.19.3 ) driver: N/A Resolution: 1368x768@60.00hz
           OpenGL: renderer: Gallium 0.4 on softpipe version: 3.3 Mesa 17.0.6
```

For use add in xorg:
```
Section "Device"
...
   Driver "sis671"
   Option "UseTiming1366" "yes"
..
EndSection
```
and use resolution 1366x768 on some notebooks (eg. ASUS K50C)

Best regards.

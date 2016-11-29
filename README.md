**Now work in xorg-1.18!!!**

This version based on Archers yaourt sisimedia-0.10.3 pathed for use with xorg-server version > 1.12
adapted by rasdark (andrew.senik@gmail.com)

For xorg-1.12 just clone and:
git checkout for-xorg-1.12

For xorg-1.18 just clone and:
git checkout for-xorg-1.18

in my gentoo box work with:
```
./configure --prefix=/usr --disable-static && make && make install
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

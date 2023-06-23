**Now work in xorg-21.1!!!**

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

For xorg-1.20 just clone and:
```
git checkout for-xorg-1.20
```

For xorg-21.1 just clone and:
```
git checkout for-xorg-21.1
```

---

Installation on debian-based distros:

1. Install dependencies:
```
sudo apt install git build-essential autoconf make libx11-dev xorg-dev xutils-dev libtool -y
```

2. Clone and select version:
```
git clone https://github.com/rasdark/xf86-video-sis671
cd xf86-video-sis671
# replace {YOUR_XORG_VERSION} with your xorg version
git checkout for-xorg-{YOUR_XORG_VERSION}
```

3. Compile:
```
autoreconf -vi
./configure --prefix=/usr --disable-static
make
```

4. Install:
```
sudo make install
```

5. Create config:
```
sudo X :0 -configure
sudo mv /root/xorg.conf.new /etc/X11/xorg.conf
```

---

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

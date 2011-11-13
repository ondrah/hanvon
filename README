Hanvon tablet driver
====================

Driver for Linux kernels which supports complete functionality of the tablet:
pen coordinates, touch/float/click detection, pressure, x and y tilt, pen
button. On Artmaster I four simple tablet buttons (note that the first
one works only together with pen activity), and the slider button.


Supported hardware
==================

Artmaster I: AM0806, AM1107, AM1209
Rollick: RL0604


Installation
============

Type 'make' to compile the module. New file hanvon.ko will be produced in
current directory. Load the module with root permissions

insmod ./hanvon.ko

If everything goes right the tablet should start working immediately.


Diagnostics
===========

After insmod, check with dmesg, if the module was loaded properly.  
"USB Hanvon tablet driver" should appear in the listing.

lsmod should also contain hanvon in its listing: lsmod | grep hanvon


Revision history
================

0.0.1 - initial release
  0.2 - corrected pressure detection, working slider button
  0.3 - remaining buttons also working, added x and y tilting
 0.3b - patch for AM1209 from Markus Zucker applied
 0.3c - patch for AM1107 from Daniel Koch applied
 0.3d - support for right side buttons of AM1107 and AM1209
  0.4 - code cleanup, RL0604 patch from Daniel Clemmer

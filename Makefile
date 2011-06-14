.PHONY: all clean archive

obj-m += hanvon.o
 
all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
 
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

archive:
	tar f - --exclude=.git -C ../ -c hanvon | gzip -c9 > ../hanvon-`date +%Y%m%d`.tgz

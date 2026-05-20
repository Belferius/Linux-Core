obj-m += simplefs.o

all: module cli

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

cli:
	gcc -Wall -Wextra -O2 -o simplefs_cli simplefs_cli.c

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f simplefs_cli

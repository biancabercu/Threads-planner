build:
	gcc -Wall -g -fPIC -c so_scheduler.c
	gcc -shared so_scheduler.o -o libscheduler.so

clean:
	rm -rf libscheduler.so

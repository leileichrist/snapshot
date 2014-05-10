TARGET=processes

all: $(TARGET)

$(TARGET): $(TARGET).o queue.o
	g++ -std=c++11 -g $(TARGET).o queue.o -o $(TARGET) -lpthread

$(TARGET).o: $(TARGET).cpp
	g++ -std=c++11 -g -c $(TARGET).cpp -lpthread

queue.o: queue.c queue.h
	g++ -std=c++11 -g -c queue.c -o queue.o -lpthread

commit:
	git add -A 
	git commit -m "mp1"
	git push -u origin master

test:
	./processes 4 f
	0 1 20 10
	0 2 10 30

clean:
	rm -rf *o $(TARGET)

clear1:
	rm snapshot*

clear2:
	rm globalState*
	
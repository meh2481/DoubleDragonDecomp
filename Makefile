SHELL=C:/Windows/System32/cmd.exe
objects = main.o wfLZ.o
LIBPATH = -L./lib
LIB = -lsquish ./lib/FreeImage.lib
HEADERPATH = -I./include
STATICGCC = -static-libgcc -static-libstdc++

all : ddd_wflz.exe
 
ddd_wflz.exe : $(objects)
	g++ -Wall -O2 -o $@ $(objects) $(LIBPATH) $(LIB) $(STATICGCC) $(HEADERPATH)
	
%.o: %.cpp
	g++ -O2 -g -ggdb -c -MMD -o $@ $< $(HEADERPATH)

-include $(objects:.o=.d)

.PHONY : clean
clean :
	rm -rf ddd_wflz.exe *.o *.d

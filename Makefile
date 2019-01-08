# ifeq ($(HOST_OS), windows)
all:
	@bash make.sh

clean:
	@find . -depth -name "*.o" -exec rm -f {} \;
run:
	cp bin/Release/wav_encode.exe ./
	./wav_encode.exe 
lib:

clean_lib:

libs:

clean_libs:


MAIN=HAMOOPI

build: clean
	cmake -Bbuild
	cmake --build build 
	cp build/${MAIN}* .
	
clean:
	rm -rf ${MAIN} *.exe *.o  *.so build

zip:
	zip -r ${MAIN}-${PLATFORM}.zip data LICENSE README.md SETUP.ini ${MAIN} ${MAIN}.exe

linux:
	PLATFORM=linux CXX=g++ make compile	zip

windows:
	PLATFORM=windows CXX=i686-w64-mingw32-g++ make compile zip

mac:
	PLATFORM=mac CXX=clang make compile zip

zip-all: linux windows mac

TARGET=HAMOOPI

# Platform detection
ifeq ($(platform),)
   platform = unix
   ifeq ($(shell uname -a),)
      platform = win
   else ifneq ($(findstring MINGW,$(shell uname -a)),)
      platform = win
   else ifneq ($(findstring Darwin,$(shell uname -a)),)
      platform = osx
   else ifneq ($(findstring win,$(shell uname -a)),)
      platform = win
   endif
endif

build: clean
	cmake -Bbuild
	cmake --build build 
	cp build/${TARGET}* .
	
clean:
	rm -rf ${TARGET} *.exe *.o  *.so build

zip:
	zip -r ${TARGET}-${platform}.zip data LICENSE README.md SETUP.ini ${TARGET} ${TARGET}.exe

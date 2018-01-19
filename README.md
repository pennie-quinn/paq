# paq #

single-file public domain C/++ libraries for game devs

inspired and architecturally informed by [stb](https://github.com/nothings/stb)
libraries


library                               | lastest version | category | LoC | description
------------------------------------- | --------------- | -------- | --- | --------------------------------
**[paq_aseprite.h](paq_aseprite.h)** | 1.01            | graphics |1634 | decode [Aseprite](https://www.aseprite.org/) files from file/memory/callbacks
**[paq_wav.h](paq_wav.ase)**         | 1.00            | audio    | 400 | load .wav files from file/memory/callbacks  

Total libraries: 2  
Total lines of C code: 2034


## General Features ##

- Any file loaders/decoders will have:
	- Load from a filepath, FILE*, memory block, or user callbacks.
	- Optionally provide your own `malloc`, `realloc`, `free`, `assert`, etc.


## FAQ ##

#### License-?
These libraries are in the public domain. Do what you want with them -- though
an attribution would be nice. And if you use them for something cool, feel free
to send me a link or something. :)

#### Why single-file?
Because you can just drop it into your project without hassle.

#### Why C?
It's my favorite. More justifiably: high portability, easier binding to all your
favorite scripting languages, etc.

#### Why public domain?
I want people to use the code if it will help them, and I don't want them to
have to deal with the silliness of licensing questions or compliance.

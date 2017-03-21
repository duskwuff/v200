V200: A TI Voyage-200 emulator

To do and notice
================

1. [Download the OS from TI's web site](https://education.ti.com/en/products/calculators/graphing-calculators/voyage-200?category=resources). The current (and probably last ever) version is v3.10, with SHA256 sum:

        a819756f84bcb5c60729b9c2fe842473d3460ae5cd211aca1d8da4e09a04dcf3  os.v2u

2. `make`

3. `./v200 os.v2u 80000000`

4. Check out the sweet screenshot in `screen.pbm`.


Wait, that's it?
----------------

For now, yeah.


License
=======

V200 is copyright (c) 2017 Dusk.

[Musashi](https://github.com/kstenerud/Musashi) is copyright (c) 1998-2001 Karl Stenerud.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

# lz77
Naive LZ77 experiment

Inspired by late night conversation with ChatGPT:

https://chatgpt.com/share/b97f9460-5f95-48a1-a1ec-4bdedf6a4c8d

(which was stupid on my part)

And then fascinating story of LZ77 here:

https://en.wikipedia.org/wiki/LZ77_and_LZ78

The lz77.h is quite trivial and achieves a little bit below 40% compression
on the text files. It lacks Huffman compression, expects whole input
to reside in memory and is not performance optimized.

It is not very useful except of understanding basic concepts of dictionary
based compression.

I just wanted to clean my mind from childhood (198x) passion for compression
and archiving.



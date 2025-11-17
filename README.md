# llmInference
A simple CLI chat interface to call API and talk to ChatGPT and Claude written in C. I wrote this because I want to utilize old machine. I am talking `pentium-m` old where it is technically internet capable, but not so in a way that you can easily use the modern web browser. 

This code is written with the help of AI. There are parts that can be optimized but I am pretty surprised myself that it is working well.

## Requirements
* `libcurl`
* `cJSON`
* some complier like `gcc`
* (optional) `TeXicode` (https://github.com/dxddxx/TeXicode) and `glow` (https://github.com/charmbracelet/glow) for markdown and latex support

## Compiling

```
gcc openrouter.c -o openrouter -lcurl -lcjson
```
or
```
gcc openrouter_md.c -o openrouter -lcurl -lcjson  
```
if you need latex and markdown support on terminal. Note that you might got a warning (and possibly error) to add `stdbool.h`, if you do just add it.

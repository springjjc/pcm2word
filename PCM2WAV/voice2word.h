#ifndef _VOICE2WORD_
#define _VOICE2WORD_
#include<stdio.h>
extern  "C" _declspec(dllexport) bool voice2wav(const char *input_path,const char *out_path);
extern  "C" _declspec(dllexport)  bool startVoice2Word(char * filename,char* error,char*rec_result);
extern  "C" _declspec(dllexport)  int wav_play(char * filename);
bool silk2pcm(FILE *bitInFile);
#endif
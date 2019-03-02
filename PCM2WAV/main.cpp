#include "voice2word.h"
#include<iostream>
using namespace std;
int main( int argc, char* argv[] )
{
	//silk2pcm("test.amr","output.pcm");
	//simplest_pcm16le_to_wave("output.pcm",1,16000,"output.wav");
	//system("pause");
	//silk2pcm(infile);
	voice2wav("1.amr","output.wav");
	wav_play("output.wav");
	/*amr2pcm("stream.amr","output.pcm");
	pcm16le_to_wave("qq11.pcm",1,16000,"output.wav");*/
	char err[400]={0};
	char result[4096]={0};
	startVoice2Word("output.wav",err,result);
	cout<<result<<endl;
	return 0;

}
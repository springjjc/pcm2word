
#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE    1
#endif
#define __STDC_CONSTANT_MACROS
#include<iostream>
#include <string.h>
#include "SKP_Silk_SDK_API.h"
#include "SKP_Silk_SigProc_FIX.h"
#include <windows.h>	/* timer */
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "SDL2/SDL.h"
};

#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"
using namespace std;

/*voice2pcm*/
#define MAX_BYTES_PER_FRAME     1024
#define MAX_INPUT_FRAMES        5
#define MAX_FRAME_LENGTH        480
#define FRAME_LENGTH_MS         20
#define MAX_API_FS_KHZ          48
#define MAX_LBRR_DELAY          2
#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

/*voice2word*/
#define	BUFFER_SIZE	4096
#define FRAME_LEN	640 
#define HINTS_SIZE  10

unsigned long GetHighResolutionTime() /* O: time in usec*/
{
	/* Returns a time counter in microsec	*/
	/* the resolution is platform dependent */
	/* but is typically 1.62 us resolution  */
	LARGE_INTEGER lpPerformanceCount;
	LARGE_INTEGER lpFrequency;
	QueryPerformanceCounter(&lpPerformanceCount);
	QueryPerformanceFrequency(&lpFrequency);
	return (unsigned long)((1000000*(lpPerformanceCount.QuadPart)) / lpFrequency.QuadPart);
}
/* Seed for the random number generator, which is used for simulating packet loss */
static SKP_int32 rand_seed = 1;
bool silk2pcm(FILE *bitInFile)
{
	unsigned long tottime, starttime;
	double    filetime;
	size_t    counter;
	SKP_int32 totPackets, i, k;
	SKP_int16 ret, len, tot_len;
	SKP_int16 nBytes;
	SKP_uint8 payload[    MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES * ( MAX_LBRR_DELAY + 1 ) ];
	SKP_uint8 *payloadEnd = NULL, *payloadToDec = NULL;
	SKP_uint8 FECpayload[ MAX_BYTES_PER_FRAME * MAX_INPUT_FRAMES ], *payloadPtr;
	SKP_int16 nBytesFEC;
	SKP_int16 nBytesPerPacket[ MAX_LBRR_DELAY + 1 ], totBytes;
	SKP_int16 out[ ( ( FRAME_LENGTH_MS * MAX_API_FS_KHZ ) << 1 ) * MAX_INPUT_FRAMES ], *outPtr;
	FILE      *speechOutFile;
	SKP_int32 packetSize_ms=0, API_Fs_Hz = 0;
	SKP_int32 decSizeBytes;
	void      *psDec;
	SKP_float loss_prob;
	SKP_int32 frames, lost;
	SKP_SILK_SDK_DecControlStruct DecControl;
	/* default settings */
	loss_prob = 0.0f;
	const char *speechOutFileName="output.pcm";
	speechOutFile = fopen( speechOutFileName, "wb" );
	if( speechOutFile == NULL ) {
		printf( "Error: could not open output file %s\n", speechOutFileName );
		return false;;
	}
	/* Set the samplingrate that is requested for the output */
	DecControl.API_sampleRate = 16000;

	/* Initialize to one frame per packet, for proper concealment before first packet arrives */
	DecControl.framesPerPacket = 1;

	/* Create decoder */
	ret = SKP_Silk_SDK_Get_Decoder_Size( &decSizeBytes );
	if( ret ) {
		printf( "\nSKP_Silk_SDK_Get_Decoder_Size returned %d", ret );
	}
	psDec = malloc( decSizeBytes );

	/* Reset decoder */
	ret = SKP_Silk_SDK_InitDecoder( psDec );
	if( ret ) {
		printf( "\nSKP_Silk_InitDecoder returned %d", ret );
	}

	totPackets = 0;
	tottime    = 0;
	payloadEnd = payload;

	/* Simulate the jitter buffer holding MAX_FEC_DELAY packets */
	for( i = 0; i < MAX_LBRR_DELAY; i++ ) {
		/* Read payload size */
		counter = fread( &nBytes, sizeof( SKP_int16 ), 1, bitInFile );
		/* Read payload */
		counter = fread( payloadEnd, sizeof( SKP_uint8 ), nBytes, bitInFile );

		if( ( SKP_int16 )counter < nBytes ) {
			break;
		}
		nBytesPerPacket[ i ] = nBytes;
		payloadEnd          += nBytes;
		totPackets++;
	}

	while( 1 ) {
		/* Read payload size */
		counter = fread( &nBytes, sizeof( SKP_int16 ), 1, bitInFile );
		if( nBytes < 0 || counter < 1 ) {
			break;
		}

		/* Read payload */
		counter = fread( payloadEnd, sizeof( SKP_uint8 ), nBytes, bitInFile );
		if( ( SKP_int16 )counter < nBytes ) {
			break;
		}

		/* Simulate losses */
		rand_seed = SKP_RAND( rand_seed );
		if( ( ( ( float )( ( rand_seed >> 16 ) + ( 1 << 15 ) ) ) / 65535.0f >= ( loss_prob / 100.0f ) ) && ( counter > 0 ) ) {
			nBytesPerPacket[ MAX_LBRR_DELAY ] = nBytes;
			payloadEnd                       += nBytes;
		} else {
			nBytesPerPacket[ MAX_LBRR_DELAY ] = 0;
		}

		if( nBytesPerPacket[ 0 ] == 0 ) {
			/* Indicate lost packet */
			lost = 1;

			/* Packet loss. Search after FEC in next packets. Should be done in the jitter buffer */
			payloadPtr = payload;
			for( i = 0; i < MAX_LBRR_DELAY; i++ ) {
				if( nBytesPerPacket[ i + 1 ] > 0 ) {
					starttime = GetHighResolutionTime();
					SKP_Silk_SDK_search_for_LBRR( payloadPtr, nBytesPerPacket[ i + 1 ], ( i + 1 ), FECpayload, &nBytesFEC );
					tottime += GetHighResolutionTime() - starttime;
					if( nBytesFEC > 0 ) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[ i + 1 ];
			}
		} else {
			lost = 0;
			nBytes = nBytesPerPacket[ 0 ];
			payloadToDec = payload;
		}

		/* Silk decoder */
		outPtr = out;
		tot_len = 0;
		starttime = GetHighResolutionTime();

		if( lost == 0 ) {
			/* No Loss: Decode all frames in the packet */
			frames = 0;
			do {
				/* Decode 20 ms */
				ret = SKP_Silk_SDK_Decode( psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len );
				if( ret ) {
					printf( "\nSKP_Silk_SDK_Decode returned %d", ret );
				}

				frames++;
				outPtr  += len;
				tot_len += len;
				if( frames > MAX_INPUT_FRAMES ) {
					/* Hack for corrupt stream that could generate too many frames */
					outPtr  = out;
					tot_len = 0;
					frames  = 0;
				}
				/* Until last 20 ms frame of packet has been decoded */
			} while( DecControl.moreInternalDecoderFrames ); 
		} else {    
			/* Loss: Decode enough frames to cover one packet duration */
			for( i = 0; i < DecControl.framesPerPacket; i++ ) {
				/* Generate 20 ms */
				ret = SKP_Silk_SDK_Decode( psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len );
				if( ret ) {
					printf( "\nSKP_Silk_Decode returned %d", ret );
				}
				outPtr  += len;
				tot_len += len;
			}
		}

		packetSize_ms = tot_len / ( DecControl.API_sampleRate / 1000 );
		tottime += GetHighResolutionTime() - starttime;
		totPackets++;

		/* Write output to file */
		fwrite( out, sizeof( SKP_int16 ), tot_len, speechOutFile );

		/* Update buffer */
		totBytes = 0;
		for( i = 0; i < MAX_LBRR_DELAY; i++ ) {
			totBytes += nBytesPerPacket[ i + 1 ];
		}
		SKP_memmove( payload, &payload[ nBytesPerPacket[ 0 ] ], totBytes * sizeof( SKP_uint8 ) );
		payloadEnd -= nBytesPerPacket[ 0 ];
		SKP_memmove( nBytesPerPacket, &nBytesPerPacket[ 1 ], MAX_LBRR_DELAY * sizeof( SKP_int16 ) );
		fprintf( stderr, "\rPackets decoded:             %d", totPackets );
	}

	/* Empty the recieve buffer */
	for( k = 0; k < MAX_LBRR_DELAY; k++ ) {
		if( nBytesPerPacket[ 0 ] == 0 ) {
			/* Indicate lost packet */
			lost = 1;

			/* Packet loss. Search after FEC in next packets. Should be done in the jitter buffer */
			payloadPtr = payload;
			for( i = 0; i < MAX_LBRR_DELAY; i++ ) {
				if( nBytesPerPacket[ i + 1 ] > 0 ) {
					starttime = GetHighResolutionTime();
					SKP_Silk_SDK_search_for_LBRR( payloadPtr, nBytesPerPacket[ i + 1 ], ( i + 1 ), FECpayload, &nBytesFEC );
					tottime += GetHighResolutionTime() - starttime;
					if( nBytesFEC > 0 ) {
						payloadToDec = FECpayload;
						nBytes = nBytesFEC;
						lost = 0;
						break;
					}
				}
				payloadPtr += nBytesPerPacket[ i + 1 ];
			}
		} else {
			lost = 0;
			nBytes = nBytesPerPacket[ 0 ];
			payloadToDec = payload;
		}

		/* Silk decoder */
		outPtr  = out;
		tot_len = 0;
		starttime = GetHighResolutionTime();

		if( lost == 0 ) {
			/* No loss: Decode all frames in the packet */
			frames = 0;
			do {
				/* Decode 20 ms */
				ret = SKP_Silk_SDK_Decode( psDec, &DecControl, 0, payloadToDec, nBytes, outPtr, &len );
				if( ret ) {
					printf( "\nSKP_Silk_SDK_Decode returned %d", ret );
				}

				frames++;
				outPtr  += len;
				tot_len += len;
				if( frames > MAX_INPUT_FRAMES ) {
					/* Hack for corrupt stream that could generate too many frames */
					outPtr  = out;
					tot_len = 0;
					frames  = 0;
				}
				/* Until last 20 ms frame of packet has been decoded */
			} while( DecControl.moreInternalDecoderFrames );
		} else {    
			/* Loss: Decode enough frames to cover one packet duration */

			/* Generate 20 ms */
			for( i = 0; i < DecControl.framesPerPacket; i++ ) {
				ret = SKP_Silk_SDK_Decode( psDec, &DecControl, 1, payloadToDec, nBytes, outPtr, &len );
				if( ret ) {
					printf( "\nSKP_Silk_Decode returned %d", ret );
				}
				outPtr  += len;
				tot_len += len;
			}
		}
		packetSize_ms = tot_len / ( DecControl.API_sampleRate / 1000 );
		tottime += GetHighResolutionTime() - starttime;
		totPackets++;
		/* Write output to file */
		fwrite( out, sizeof( SKP_int16 ), tot_len, speechOutFile );
		/* Update Buffer */
		totBytes = 0;
		for( i = 0; i < MAX_LBRR_DELAY; i++ ) {
			totBytes += nBytesPerPacket[ i + 1 ];
		}
		SKP_memmove( payload, &payload[ nBytesPerPacket[ 0 ] ], totBytes * sizeof( SKP_uint8 ) );
		payloadEnd -= nBytesPerPacket[ 0 ];
		SKP_memmove( nBytesPerPacket, &nBytesPerPacket[ 1 ], MAX_LBRR_DELAY * sizeof( SKP_int16 ) );

		fprintf( stderr, "\rPackets decoded:              %d", totPackets );

	}
	printf( "\nDecoding Finished \n" );
	/* Free decoder */
	free( psDec );
	/* Close files */
	fclose(bitInFile);
	fclose( speechOutFile );
	filetime = totPackets * 1e-3 * packetSize_ms;
	printf("\nFile length:                 %.3f s", filetime);
	printf("\nTime for decoding:           %.3f s (%.3f%% of realtime)", 1e-6 * tottime, 1e-4 * tottime / filetime);
	printf("\n\n");
	return true;
}
bool pcm16le_to_wave(const char *pcmpath,int channels,int sample_rate,const char *wavepath)
{

	typedef struct WAVE_HEADER{  
		char         fccID[4];        
		unsigned   long    dwSize;            
		char         fccType[4];    
	}WAVE_HEADER;  

	typedef struct WAVE_FMT{  
		char         fccID[4];        
		unsigned   long       dwSize;            
		unsigned   short     wFormatTag;    
		unsigned   short     wChannels;  
		unsigned   long       dwSamplesPerSec;  
		unsigned   long       dwAvgBytesPerSec;  
		unsigned   short     wBlockAlign;  
		unsigned   short     uiBitsPerSample;  
	}WAVE_FMT;  

	typedef struct WAVE_DATA{  
		char       fccID[4];          
		unsigned long dwSize;              
	}WAVE_DATA;  


	if(channels==0||sample_rate==0){
		channels = 2;
		sample_rate = 44100;
	}
	int bits = 16;

	WAVE_HEADER   pcmHEADER;  
	WAVE_FMT   pcmFMT;  
	WAVE_DATA   pcmDATA;  

	unsigned   short   m_pcmData;
	FILE   *fp,*fpout;  

	fp=fopen(pcmpath, "rb");
	if(fp == NULL) {  
		printf("open pcm file error\n");
		return false;  
	}
	fpout=fopen(wavepath,   "wb+");
	if(fpout == NULL) {    
		printf("create wav file error\n");  
		return false; 
	}        
	//WAVE_HEADER
	memcpy(pcmHEADER.fccID,"RIFF",strlen("RIFF"));                    
	memcpy(pcmHEADER.fccType,"WAVE",strlen("WAVE"));  
	fseek(fpout,sizeof(WAVE_HEADER),1); 
	//WAVE_FMT
	pcmFMT.dwSamplesPerSec=sample_rate;  
	pcmFMT.dwAvgBytesPerSec=pcmFMT.dwSamplesPerSec*sizeof(m_pcmData);  
	pcmFMT.uiBitsPerSample=bits;
	memcpy(pcmFMT.fccID,"fmt ",strlen("fmt "));  
	pcmFMT.dwSize=16;  
	pcmFMT.wBlockAlign=2;  
	pcmFMT.wChannels=channels;  
	pcmFMT.wFormatTag=1;  

	fwrite(&pcmFMT,sizeof(WAVE_FMT),1,fpout); 

	//WAVE_DATA;
	memcpy(pcmDATA.fccID,"data",strlen("data"));  
	pcmDATA.dwSize=0;
	fseek(fpout,sizeof(WAVE_DATA),SEEK_CUR);

	fread(&m_pcmData,sizeof(unsigned short),1,fp);
	while(!feof(fp)){  
		pcmDATA.dwSize+=2;
		fwrite(&m_pcmData,sizeof(unsigned short),1,fpout);
		fread(&m_pcmData,sizeof(unsigned short),1,fp);
	}  

	pcmHEADER.dwSize=44+pcmDATA.dwSize;

	rewind(fpout);
	fwrite(&pcmHEADER,sizeof(WAVE_HEADER),1,fpout);
	fseek(fpout,sizeof(WAVE_FMT),SEEK_CUR);
	fwrite(&pcmDATA,sizeof(WAVE_DATA),1,fpout);

	fclose(fp);
	fclose(fpout);

	return true;
}

bool amr2pcm(const char *amrpath,const char *pcmpath)
{
	AVFormatContext	*pFormatCtx;
	int				i, audioStream;
	AVCodecContext	*pCodecCtx;
	AVCodec			*pCodec;
	AVPacket		*packet;
	uint8_t			*out_buffer;
	AVFrame			*pFrame;
	int ret;
	uint32_t len = 0;
	int got_picture;
	int index = 0;
	int64_t in_channel_layout;
	struct SwrContext *au_convert_ctx;

	FILE *pFile=fopen(pcmpath, "wb");
	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	//Open
	if(avformat_open_input(&pFormatCtx,amrpath,NULL,NULL)!=0){
		printf("Couldn't open input stream.\n");
		return false;
	}
	// Retrieve stream information
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		printf("Couldn't find stream information.\n");
		return false;
	}
	// Dump valid information onto standard error
	av_dump_format(pFormatCtx, 0, amrpath, false);

	// Find the first audio stream
	audioStream=-1;
	for(i=0; i < pFormatCtx->nb_streams; i++)
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO){
			audioStream=i;
			break;
		}

		if(audioStream==-1){
			printf("Didn't find a audio stream.\n");
			return false;
		}

		// Get a pointer to the codec context for the audio stream
		pCodecCtx=pFormatCtx->streams[audioStream]->codec;

		// Find the decoder for the audio stream
		pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
		if(pCodec==NULL){
			printf("Codec not found.\n");
			return false;
		}

		// Open codec
		if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
			printf("Could not open codec.\n");
			return false;
		}

		packet=(AVPacket *)av_malloc(sizeof(AVPacket));
		av_init_packet(packet);

		//Out Audio Param
		uint64_t out_channel_layout=AV_CH_LAYOUT_MONO;
		//nb_samples: AAC-1024 MP3-1152
		int out_nb_samples=320;
		AVSampleFormat out_sample_fmt=AV_SAMPLE_FMT_S16;
		int out_sample_rate=16000;
		int out_channels=av_get_channel_layout_nb_channels(out_channel_layout);
		//Out Buffer Size
		int out_buffer_size=av_samples_get_buffer_size(NULL,out_channels ,out_nb_samples,out_sample_fmt, 1);

		out_buffer=(uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
		pFrame=av_frame_alloc();

		//FIX:Some Codec's Context Information is missing
		in_channel_layout=av_get_default_channel_layout(pCodecCtx->channels);
		//Swr
		au_convert_ctx = swr_alloc();
		au_convert_ctx=swr_alloc_set_opts(au_convert_ctx,out_channel_layout, out_sample_fmt, out_sample_rate,
			in_channel_layout,pCodecCtx->sample_fmt , pCodecCtx->sample_rate,0, NULL);
		swr_init(au_convert_ctx);

		while(av_read_frame(pFormatCtx, packet)>=0){
			if(packet->stream_index==audioStream){

				ret = avcodec_decode_audio4( pCodecCtx, pFrame,&got_picture, packet);
				if ( ret < 0 ) {
					printf("Error in decoding audio frame.\n");
					return false;
				}
				if ( got_picture > 0 ){
					swr_convert(au_convert_ctx,&out_buffer, MAX_AUDIO_FRAME_SIZE,(const uint8_t **)pFrame->data , pFrame->nb_samples);

					printf("index:%5d\t pts:%lld\t packet size:%d\n",index,packet->pts,packet->size);
					//Write PCM
					fwrite(out_buffer, 1, out_buffer_size, pFile);
					index++;
				}
			}
			av_free_packet(packet);
		}

		swr_free(&au_convert_ctx);

		fclose(pFile);

		av_free(out_buffer);
		// Close the codec
		avcodec_close(pCodecCtx);
		// Close the video file
		avformat_close_input(&pFormatCtx);
		return true;
}

enum AudioType
{
	Unknown_TYPE,
	SILK_TYPE,
	AMR_TYPE
};
AudioType getAudioType(FILE *inFile)
{
	char audio_type[10]={0};
	fseek(inFile,1,SEEK_SET);
	fread( audio_type, sizeof( char ), strlen( "#!SILK_V3" ), inFile);
	audio_type[ strlen( "#!SILK_V3" ) ] = '\0'; /* Terminate with a null character */
	if( strcmp(audio_type, "#!SILK_V3" ) == 0 ) { 
		return SILK_TYPE;
	}
	else
	{
		fseek(inFile,0,SEEK_SET);
		fread( audio_type, sizeof( char ), strlen( "#!AMR" ), inFile);
		audio_type[strlen( "#!AMR" )] = '\0';
		if( strcmp(audio_type, "#!AMR" ) == 0 ) { 
			return AMR_TYPE;
		}
	}
	return Unknown_TYPE;
}
extern  "C" _declspec(dllexport) bool voice2wav(const char *input_path,const char *out_path)
{
	FILE *inFile=fopen(input_path,"rb");
	AudioType voice_type=getAudioType(inFile);
	bool ret=false;
	if(voice_type==SILK_TYPE)
	{
		ret=silk2pcm(inFile);
	}
	else if(voice_type==AMR_TYPE)
	{
		fclose(inFile);
		ret=amr2pcm(input_path,"output.pcm");

	}
	else
	{
		fclose(inFile);
	}
	if(ret==false)
		return false;
	ret=pcm16le_to_wave("output.pcm",1,16000,out_path);
	//DeleteFile("output.pcm");
	return ret;
}
extern  "C" _declspec(dllexport) bool startVoice2Word(char * filename,char* error,char*rec_result)
{
	strcpy(error,"jiang");
	string errorMsg="";
	int			ret = MSP_SUCCESS;
	int			upload_on = 1; //是否上传用户词表
	const char* login_params = "appid = 5bdbb18a, work_dir = ."; // 登录参数，appid与msc库绑定,请勿随意改动
	/*
	* sub:				请求业务类型
	* domain:			领域
	* language:			语言
	* accent:			方言
	* sample_rate:		音频采样率
	* result_type:		识别结果格式
	* result_encoding:	结果编码格式
	*
	*/
	const char* session_begin_params = "sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = gb2312";

	/* 用户登录 */
	ret = MSPLogin(NULL, NULL, login_params); //第一个参数是用户名，第二个参数是密码，均传NULL即可，第三个参数是登录参数	
	if (MSP_SUCCESS != ret)
	{
		errorMsg = "MSPLogin failed , Error code: " + ret;
		return "";
	}

	//开始识别
	//run_iat(filename.c_str(), session_begin_params);

	{
		const char*		session_id = NULL;
		char			hints[HINTS_SIZE] = { NULL }; //hints为结束本次会话的原因描述
		unsigned int	total_len = 0;
		int				aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;		//音频状态
		int				ep_stat = MSP_EP_LOOKING_FOR_SPEECH;		//端点检测
		int				rec_stat = MSP_REC_STATUS_SUCCESS;			//识别状态
		int				errcode = MSP_SUCCESS;
		const char*		audio_file = filename;

		FILE*			f_pcm = NULL;
		char*			p_pcm = NULL;
		long			pcm_count = 0;
		long			pcm_size = 0;
		long			read_size = 0;
		int				waitTime;

		if (NULL == audio_file)
			goto iat_exit;

		f_pcm = fopen(audio_file, "rb");
		if (NULL == f_pcm)
		{
			//printf("\nopen [%s] failed! \n", audio_file);
			//goto iat_exit;
			errorMsg = "open  failed!";
			return false;
		}

		fseek(f_pcm, 0, SEEK_END);
		pcm_size = ftell(f_pcm); //获取音频文件大小 
		fseek(f_pcm, 0, SEEK_SET);

		p_pcm = (char *)malloc(pcm_size);
		if (NULL == p_pcm)
		{
			errorMsg = "out of memory! ";
			return false;
		}

		read_size = fread((void *)p_pcm, 1, pcm_size, f_pcm); //读取音频文件内容
		waitTime = read_size / 400;
		if (read_size != pcm_size)
		{
			//printf("\nread [%s] error!\n", audio_file);
			//goto iat_exit;
			errorMsg = "read error!";
			return false;
		}

		//printf("\n开始语音听写 ...\n");
		//开始一次语音识别，获得句柄，句柄将用于请求识别结果
		session_id = QISRSessionBegin(NULL, session_begin_params, &errcode); //听写不需要语法，第一个参数为NULL
		if (MSP_SUCCESS != errcode)
		{
			errorMsg = "QISRSessionBegin failed!error code : " + errcode;//std::string("QISRSessionBegin failed! error code:%d", errcode);
			return false;
		}

		//上传语音
		while (1)
		{
			unsigned int len = 10 * FRAME_LEN; // 每次写入200ms音频(16k，16bit)：1帧音频20ms，10帧=200ms。16k采样率的16位音频，一帧的大小为640Byte
			int ret = 0;

			if (pcm_size < 2 * len)
				len = pcm_size;
			//如果音频很短，可能上传完了还没有识别出结果
			if (len <= 0)
				break;

			aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
			if (0 == pcm_count)
				aud_stat = MSP_AUDIO_SAMPLE_FIRST;

			//写入本次识别的音频
			ret = QISRAudioWrite(session_id, (const void *)&p_pcm[pcm_count], len, aud_stat, &ep_stat, &rec_stat);
			if (MSP_SUCCESS != ret)
			{
				//printf("\nQISRAudioWrite failed! error code:%d\n", ret);
				//goto iat_exit;
				errorMsg = "QISRAudioWrite failed! error code:" + ret;
				return false;
			}

			pcm_count += (long)len;
			pcm_size -= (long)len;

		}
		//写入本次识别的音频
		//第二个参数为上传音频起始地址，NULL则表示上传为空
		//第四个参数表示告诉服务器已经全部上传完成
		errcode = QISRAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_stat, &rec_stat);
		if (MSP_SUCCESS != errcode)
		{
			//printf("\nQISRAudioWrite failed! error code:%d \n", errcode);
			//goto iat_exit;
			errorMsg = "QISRAudioWrite failed! error code:" + errcode;
			return false;
		}
		//上传语音结束

		//等待语音识别结果
		while (MSP_REC_STATUS_COMPLETE != rec_stat)
		{
			const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
			if (MSP_SUCCESS != errcode)
			{
				//printf("\nQISRGetResult failed, error code: %d\n", errcode);
				//goto iat_exit;
				errorMsg = "QISRGetResult failed, error code: " + errcode;
				return false;
			}
			if (NULL != rslt)
			{
				unsigned int rslt_len = strlen(rslt);
				total_len += rslt_len;
				if (total_len >= BUFFER_SIZE)
				{
					//printf("\nno enough buffer for rec_result !\n");
					//goto iat_exit;
					errorMsg = "no enough buffer for rec_result !";
					return "";
				}
				strncat(rec_result, rslt, rslt_len);
			}
			Sleep(150); //防止频繁占用CPU
		}
iat_exit:
		if (NULL != f_pcm)
		{
			fclose(f_pcm);
			f_pcm = NULL;
		}
		if (NULL != p_pcm)
		{
			free(p_pcm);
			p_pcm = NULL;
		}

		//结束本次语言识别
		QISRSessionEnd(session_id, hints);
	}

	MSPLogout(); //退出登录

	errorMsg = "succeed";
	return true;
}

static  Uint8  *audio_chunk; 
static  Uint32  audio_len; 
static  Uint8  *audio_pos; 

void  fill_audio(void *udata,Uint8 *stream,int len){ 
	//SDL 2.0
	SDL_memset(stream, 0, len);
	if(audio_len==0)
		return; 

	len=(len>audio_len?audio_len:len);	/*  Mix  as  much  data  as  possible  */ 

	SDL_MixAudio(stream,audio_pos,len,SDL_MIX_MAXVOLUME);
	audio_pos += len; 
	audio_len -= len; 
} 

extern  "C" _declspec(dllexport)  int wav_play(char * filename)
{
	AVFormatContext	*pFormatCtx;
	int				i, audioStream;
	AVCodecContext	*pCodecCtx;
	AVCodec			*pCodec;
	AVPacket		*packet;
	uint8_t			*out_buffer;
	AVFrame			*pFrame;
	SDL_AudioSpec wanted_spec;
	int ret;
	uint32_t len = 0;
	int got_picture;
	int index = 0;
	int64_t in_channel_layout;
	struct SwrContext *au_convert_ctx;

	FILE *pFile=NULL;
	//char url[]="output.wav";

	av_register_all();
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	//Open
	if(avformat_open_input(&pFormatCtx,filename,NULL,NULL)!=0){
		printf("Couldn't open input stream.\n");
		return 0;
	}
	// Retrieve stream information
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		printf("Couldn't find stream information.\n");
		return 1;
	}
	// Dump valid information onto standard error
	av_dump_format(pFormatCtx, 0, filename, false);

	// Find the first audio stream
	audioStream=-1;
	for(i=0; i < pFormatCtx->nb_streams; i++)
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO){
			audioStream=i;
			break;
		}

		if(audioStream==-1){
			printf("Didn't find a audio stream.\n");
			return 2;
		}

		// Get a pointer to the codec context for the audio stream
		pCodecCtx=pFormatCtx->streams[audioStream]->codec;

		// Find the decoder for the audio stream
		pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
		if(pCodec==NULL){
			printf("Codec not found.\n");
			return 3;
		}

		// Open codec
		if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
			printf("Could not open codec.\n");
			return 4;
		}

		packet=(AVPacket *)av_malloc(sizeof(AVPacket));
		av_init_packet(packet);

		//Out Audio Param
		uint64_t out_channel_layout=AV_CH_LAYOUT_MONO;
		//nb_samples: AAC-1024 MP3-1152
		int out_nb_samples=2048;
		AVSampleFormat out_sample_fmt=AV_SAMPLE_FMT_S16;
		int out_sample_rate=16000;
		int out_channels=av_get_channel_layout_nb_channels(out_channel_layout);
		//Out Buffer Size
		int out_buffer_size=av_samples_get_buffer_size(NULL,out_channels ,out_nb_samples,out_sample_fmt, 1);

		out_buffer=(uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
		//out_buffer=(uint8_t *)av_malloc(640);
		pFrame=av_frame_alloc();
		//SDL------------------

		if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {  
			printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
			return 5;
		}
		//SDL_AudioSpec
		wanted_spec.freq = out_sample_rate; 
		wanted_spec.format = AUDIO_S16SYS; 
		wanted_spec.channels = out_channels; 
		wanted_spec.silence = 0; 
		wanted_spec.samples = out_nb_samples; 
		wanted_spec.callback = fill_audio; 
		wanted_spec.userdata = pCodecCtx; 

		if (SDL_OpenAudio(&wanted_spec, NULL)<0){ 
			printf("can't open audio.\n"); 
			return 6; 
		} 
		//FIX:Some Codec's Context Information is missing
		in_channel_layout=av_get_default_channel_layout(pCodecCtx->channels);
		//Swr
		au_convert_ctx = swr_alloc();
		au_convert_ctx=swr_alloc_set_opts(au_convert_ctx,out_channel_layout, out_sample_fmt, out_sample_rate,
			in_channel_layout,pCodecCtx->sample_fmt , pCodecCtx->sample_rate,0, NULL);
		swr_init(au_convert_ctx);
		//Play
		SDL_PauseAudio(0);

		while(av_read_frame(pFormatCtx, packet)>=0){
			if(packet->stream_index==audioStream){
				ret = avcodec_decode_audio4( pCodecCtx, pFrame,&got_picture, packet);
				if ( ret < 0 ) {
					printf("Error in decoding audio frame.\n");
					return 7;
				}
				if ( got_picture > 0 ){
					swr_convert(au_convert_ctx,&out_buffer, MAX_AUDIO_FRAME_SIZE,(const uint8_t **)pFrame->data , pFrame->nb_samples);
					printf("index:%5d\t pts:%lld\t packet size:%d\n",index,packet->pts,packet->size);
					index++;
				}
				while(audio_len>0)//Wait until finish
					SDL_Delay(1); 

				//Set audio buffer (PCM data)
				audio_chunk = (Uint8 *) out_buffer; 
				//Audio buffer length
				audio_len =out_buffer_size;
				audio_pos = audio_chunk;

			}
			av_free_packet(packet);
		}
		swr_free(&au_convert_ctx);
		SDL_CloseAudio();//Close SDL
		SDL_Quit();
		av_free(out_buffer);
		avcodec_close(pCodecCtx);
		avformat_close_input(&pFormatCtx);
		return 8;
}
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace test
{
    class Program
    {
        [DllImport("democpp.dll", EntryPoint = "Add", CharSet = CharSet.Auto, CallingConvention = CallingConvention.Cdecl)]
        extern static int Add(int x, int y);

        [DllImport("democpp.dll", EntryPoint = "Sub", CharSet = CharSet.Auto, CallingConvention = CallingConvention.Cdecl)]
        extern static int Sub(int x, int y);
        [DllImport("democpp.dll", EntryPoint = "startV2W", CharSet = CharSet.Auto, CallingConvention = CallingConvention.Cdecl)]
        extern static void startV2W();
        [DllImport("democpp.dll", EntryPoint = "strTest", CharSet = CharSet.Auto, CallingConvention = CallingConvention.Cdecl)]
        extern static IntPtr strTest(IntPtr filename);
        [DllImport("voice2word.dll", EntryPoint = "startVoice2Word", CharSet = CharSet.Auto, CallingConvention = CallingConvention.Cdecl)]
        extern static bool startVoice2Word(IntPtr filename, IntPtr errorMsg, IntPtr rec_result);

        [DllImport("voice2word.dll", EntryPoint = "voice2wav", CharSet = CharSet.Auto, CallingConvention = CallingConvention.Cdecl)]
        extern static bool voice2wav(IntPtr filename, IntPtr outpath);

        [DllImport("voice2word.dll", EntryPoint = "wav_play", CharSet = CharSet.Auto, CallingConvention = CallingConvention.Cdecl)]
        extern static int wav_play(IntPtr filename);

        static void test(ref string s)
        {
            s = "jiang"; 
        }
  
        static void Main(string[] args)
        {
            //Console.WriteLine(Add(10, 2).ToString());
            //Console.WriteLine(Sub(10, 2).ToString());
           // startV2W(); 
            //string srcpath=@"C:\Users\Administrator\Desktop\voicedll\PCM2WAV\PCM2WAV";
            //IntPtr filename1 = Marshal.StringToHGlobalAnsi(srcpath+@"/"+"test.amr");
            //IntPtr filename = Marshal.StringToHGlobalAnsi("test.amr");
            //IntPtr outpath = Marshal.StringToHGlobalAnsi("E:\\hunterdata\\明喜\\data\\AppDomain\\com.tencent.mqq\\Documents\\363703156\\Audio\\10j3roNEQmhHHiDoZBp1iPijOf0vQnsh7lBC.wav");
            //voice2wav(filename1, outpath);
            //wav_play(outpath);



            IntPtr ptrIn = Marshal.StringToHGlobalAnsi("1.wav");
            IntPtr ptrIn1 = Marshal.AllocHGlobal(500);
            //IntPtr ptrRet = Marshal.AllocHGlobal(4096);
            byte[] buf = new byte[4096];
            bool ret = startVoice2Word(ptrIn, ptrIn1, Marshal.UnsafeAddrOfPinnedArrayElement(buf, 0));
            //string retlust = Marshal.PtrToStringAnsi(ptrRet);
            string err = Marshal.PtrToStringAnsi(ptrIn1);
            byte[] cvtBuf = new byte[4096];
            cvtBuf = Encoding.Convert(Encoding.Default, Encoding.Unicode, buf, 0, 4096 - 1);
            string recvStr = Marshal.PtrToStringAuto(Marshal.UnsafeAddrOfPinnedArrayElement(cvtBuf, 0));
            //Console.WriteLine(Marshal.PtrToStringAuto(ptrRet));
            Marshal.Release(ptrIn);
            Marshal.Release(ptrIn1);
            //Marshal.Release(ptrRet);
            //Console.WriteLine(retlust);
            Console.WriteLine(recvStr);


            Console.Read();
        }
    }
}

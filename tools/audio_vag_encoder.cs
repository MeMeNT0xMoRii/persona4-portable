// Offline PSX ADPCM encoder for SAS streaming. No game code runs this encoder.
using System;
using System.IO;

class AudioVagEncoder {
    static readonly int[] C1 = {0, 60, 115, 98, 122};
    static readonly int[] C2 = {0, 0, -52, -55, -60};
    static int Clamp(int x) { return Math.Max(-32768, Math.Min(32767, x)); }
    static void Encode(short[] pcm, int count, byte[] dst, ref int h1, ref int h2, bool reset) {
        long best = long.MaxValue;
        byte[] candidate = new byte[16];
        int best1 = 0, best2 = 0;
        for (int filter = 0; filter < (reset ? 1 : 5); ++filter) {
            int maxError = 0, a = h1, b = h2;
            for (int i = 0; i < 28; ++i) {
                int value = i < count ? pcm[i] : 0;
                maxError = Math.Max(maxError, Math.Abs(value - ((a*C1[filter]+b*C2[filter]+32)>>6)));
                b = a; a = value;
            }
            int shift = 12;
            while (shift > 0 && maxError > (7 << (12-shift))) --shift;
            // Try neighbouring scales as predictor feedback changes the residuals.
            for (int s = Math.Max(0, shift-1); s <= Math.Min(12, shift+1); ++s) {
                Array.Clear(candidate, 0, candidate.Length);
                candidate[0] = (byte)((filter << 4) | s);
                long error = 0; a = h1; b = h2;
                int step = 1 << (12-s);
                for (int i = 0; i < 28; ++i) {
                    int value = i < count ? pcm[i] : 0;
                    int prediction = (a*C1[filter]+b*C2[filter]+32)>>6;
                    int q = Math.Max(-8, Math.Min(7, (int)Math.Round((value-prediction)/(double)step)));
                    int reconstructed = Clamp(prediction+q*step);
                    long e = value-reconstructed; error += e*e;
                    candidate[2+i/2] |= (byte)((q & 15) << ((i & 1)*4));
                    b=a; a=reconstructed;
                }
                if (error < best) {
                    best=error; best1=a; best2=b;
                    Buffer.BlockCopy(candidate,0,dst,0,16);
                }
            }
        }
        h1=best1; h2=best2;
    }
    static int Main(string[] args) {
        if (args.Length < 3) { Console.Error.WriteLine("encoder input.s16le output.blocks channels [loop-block]"); return 2; }
        int channels = int.Parse(args[2]);
        long loopBlock = args.Length > 3 ? long.Parse(args[3]) : -1;
        if (channels < 1 || channels > 2) return 2;
        using (var input = new BinaryReader(File.OpenRead(args[0])))
        using (var output = File.Create(args[1])) {
            if (input.BaseStream.Length % (channels*2) != 0) throw new Exception("unaligned PCM");
            long frames = input.BaseStream.Length/(channels*2), block=0;
            int[] h1=new int[channels],h2=new int[channels];
            short[][] pcm=new short[channels][];
            for (int c=0;c<channels;++c) pcm[c]=new short[28];
            byte[] encoded=new byte[16];
            while (frames>0) {
                int n=(int)Math.Min(28,frames);
                for(int i=0;i<n;++i) for(int c=0;c<channels;++c) pcm[c][i]=input.ReadInt16();
                for(int c=0;c<channels;++c) {
                    // Independent 256-block chunks permit source loops and ring refills.
                    Encode(pcm[c],n,encoded,ref h1[c],ref h2[c],block%256==0 || block==loopBlock);
                    output.Write(encoded,0,16);
                }
                frames-=n; ++block;
            }
        }
        return 0;
    }
}

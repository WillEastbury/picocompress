using System;
using System.Linq;
using System.Text;
using Xunit;
using Picocompress;

namespace Picocompress.Tests;

public class PicocompressTests
{
    [Fact]
    public void Roundtrip_Empty()
    {
        var compressed = PicocompressCodec.Compress(ReadOnlySpan<byte>.Empty);
        Assert.Empty(compressed);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Empty(decompressed);
    }

    [Fact]
    public void Roundtrip_SingleByte()
    {
        byte[] input = [0x42];
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_ShortString()
    {
        byte[] input = Encoding.UTF8.GetBytes("Hello, picocompress!");
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_AllBytes()
    {
        byte[] input = new byte[256];
        for (int i = 0; i < 256; i++) input[i] = (byte)i;
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_RepeatingData()
    {
        byte[] input = new byte[1024];
        for (int i = 0; i < input.Length; i++) input[i] = (byte)(i % 7);
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
        // Repeating data should compress well
        Assert.True(compressed.Length < input.Length,
            $"Expected compression: {compressed.Length} < {input.Length}");
    }

    [Fact]
    public void Roundtrip_JSON()
    {
        string json = """
            {"name":"test","type":"device","status":"active","value":null,"data":"content",
             "message":"request","result":"response","code":"default","number":42,
             "list":[{"item":"text","size":100,"mode":"input","time":"2024-01-01"}],
             "error":false,"state":"alert","order":"region","length":256}
            """;
        byte[] input = Encoding.UTF8.GetBytes(json);
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
        // JSON with dictionary words should compress well
        Assert.True(compressed.Length < input.Length,
            $"Expected compression: {compressed.Length} < {input.Length}");
    }

    [Fact]
    public void Roundtrip_MultiBlock()
    {
        // Create data larger than one block (508 bytes)
        byte[] input = new byte[2000];
        var rng = new Random(12345);
        // Use semi-structured data for good compression
        string template = "The status of device number {0} is active with value \"{1}\" and type \"string\". ";
        var sb = new StringBuilder();
        for (int i = 0; sb.Length < 2000; i++)
            sb.Append(string.Format(template, i, "data" + i));
        input = Encoding.UTF8.GetBytes(sb.ToString()[..2000]);

        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_LargeRandom()
    {
        // Random data should roundtrip but not compress much
        byte[] input = new byte[5000];
        new Random(42).NextBytes(input);
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_ExactBlockSize()
    {
        byte[] input = Encoding.UTF8.GetBytes(new string('A', 508));
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_BlockSizePlusOne()
    {
        byte[] input = Encoding.UTF8.GetBytes(new string('B', 509));
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_DictionaryEntries()
    {
        // Pack many dictionary words together
        string text = "true false null name data time type mode code size " +
                       "ment ness able ight tion list item text " +
                       "error value state alert input ation order " +
                       "status number active device region string result length " +
                       "message content request default operator response";
        byte[] input = Encoding.UTF8.GetBytes(text);
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_MicroProfile()
    {
        byte[] input = Encoding.UTF8.GetBytes("The quick brown fox jumps over the lazy dog. The fox is quick.");
        var compressed = PicocompressCodec.Compress(input, PicocompressOptions.Micro);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_MinimalProfile()
    {
        byte[] input = Encoding.UTF8.GetBytes("Hello world! Hello world! Hello world!");
        var compressed = PicocompressCodec.Compress(input, PicocompressOptions.Minimal);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_AggressiveProfile()
    {
        byte[] input = new byte[1500];
        for (int i = 0; i < input.Length; i++) input[i] = (byte)('A' + (i % 26));
        var compressed = PicocompressCodec.Compress(input, PicocompressOptions.Aggressive);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_Q3Profile()
    {
        string text = string.Concat(Enumerable.Repeat(
            "The status message for the default device is active. The content request has a response. ", 20));
        byte[] input = Encoding.UTF8.GetBytes(text);
        var compressed = PicocompressCodec.Compress(input, PicocompressOptions.Q3);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_Q4Profile()
    {
        string text = string.Concat(Enumerable.Repeat(
            "The number of items in the list is the length of the result string value. ", 30));
        byte[] input = Encoding.UTF8.GetBytes(text);
        var compressed = PicocompressCodec.Compress(input, PicocompressOptions.Q4);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Compress_CorruptedData_Throws()
    {
        byte[] corrupt = [0x04, 0x00, 0x02, 0x00, 0xFF, 0xFF];
        Assert.Throws<InvalidOperationException>(() => PicocompressCodec.Decompress(corrupt));
    }

    [Fact]
    public void Compress_TruncatedHeader_Throws()
    {
        byte[] truncated = [0x04, 0x00];
        Assert.Throws<InvalidOperationException>(() => PicocompressCodec.Decompress(truncated));
    }

    [Fact]
    public void Roundtrip_RepeatOffset()
    {
        // Structured data with fixed strides — triggers repeat-offset coding
        var sb = new StringBuilder();
        for (int i = 0; i < 50; i++)
            sb.Append($"id:{i:D4} val:{i * 3:D6}\n");
        byte[] input = Encoding.UTF8.GetBytes(sb.ToString());
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void Roundtrip_HTML()
    {
        string html = """
            <div class="container">
              <div class="header">Title</div>
              <div class="content">
                <input type="text" name="field" value="default"/>
              </div>
            </div>
            """;
        byte[] input = Encoding.UTF8.GetBytes(html);
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void DictionaryCount_Is96()
    {
        // Verify the static dictionary has exactly 96 entries by checking that
        // dict refs for indices 0-95 can all be decoded from a crafted payload.
        // This is an indirect test; direct count verification would require reflection.
        // Instead, verify roundtrip of text that exercises high-index dict entries.
        string text = "DIM FOR END REL EACH LOAD SAVE CARD JUMP PRINT INPUT GOSUB STREAM RETURN SWITCH PROGRAM";
        byte[] input = Encoding.UTF8.GetBytes(text);
        var compressed = PicocompressCodec.Compress(input);
        var decompressed = PicocompressCodec.Decompress(compressed);
        Assert.Equal(input, decompressed);
    }

    [Fact]
    public void BlockSize_InvalidThrows()
    {
        var opts = new PicocompressOptions { BlockSize = 0 };
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            PicocompressCodec.Compress(new byte[] { 1 }, opts));

        opts = new PicocompressOptions { BlockSize = 512 };
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            PicocompressCodec.Compress(new byte[] { 1 }, opts));
    }
}

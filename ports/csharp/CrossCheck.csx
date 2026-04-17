using System;
using System.Text;
using Picocompress;

// Test 1: simple ASCII
byte[] input1 = Encoding.ASCII.GetBytes("Hello, World!");
byte[] comp1 = PicocompressCodec.Compress(input1);
Console.WriteLine("TEST1_HEX=" + BitConverter.ToString(comp1).Replace("-",""));

// Test 2: JSON
byte[] input2 = Encoding.ASCII.GetBytes("{\"name\":\"test\",\"type\":\"value\",\"status\":\"active\"}");
byte[] comp2 = PicocompressCodec.Compress(input2);
Console.WriteLine("TEST2_HEX=" + BitConverter.ToString(comp2).Replace("-",""));

// Test 3: repeated data
byte[] input3 = new byte[200];
for (int i = 0; i < 200; i++) input3[i] = (byte)'A';
byte[] comp3 = PicocompressCodec.Compress(input3);
Console.WriteLine("TEST3_HEX=" + BitConverter.ToString(comp3).Replace("-",""));

// Test 4: cross-block (>508 bytes repeated pattern)
byte[] pattern = Encoding.ASCII.GetBytes("abcdefghijklmnopqrstuvwxyz");
byte[] input4 = new byte[1200];
for (int i = 0; i < 1200; i++) input4[i] = pattern[i % pattern.Length];
byte[] comp4 = PicocompressCodec.Compress(input4);
Console.WriteLine("TEST4_HEX=" + BitConverter.ToString(comp4).Replace("-",""));

package picocompress;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Random;
import java.util.stream.Stream;

import static org.junit.jupiter.api.Assertions.*;

class PicocompressTest {

    // ---- Roundtrip helpers ----

    private static void assertRoundtrip(byte[] original) {
        assertRoundtrip(original, Picocompress.Options.DEFAULT);
    }

    private static void assertRoundtrip(byte[] original, Picocompress.Options opts) {
        byte[] compressed = Picocompress.compress(original, opts);
        byte[] decompressed = Picocompress.decompress(compressed);
        assertArrayEquals(original, decompressed,
            "Roundtrip failed for input of length " + original.length
                + " (compressed " + compressed.length + " bytes)");
    }

    // ---- Basic roundtrip tests ----

    @Test
    @DisplayName("Empty input")
    void emptyInput() {
        assertRoundtrip(new byte[0]);
    }

    @Test
    @DisplayName("Single byte")
    void singleByte() {
        assertRoundtrip(new byte[]{42});
    }

    @Test
    @DisplayName("Two bytes")
    void twoBytes() {
        assertRoundtrip(new byte[]{0x00, (byte) 0xFF});
    }

    @Test
    @DisplayName("Short ASCII string")
    void shortAscii() {
        assertRoundtrip("Hello, World!".getBytes(StandardCharsets.US_ASCII));
    }

    @Test
    @DisplayName("Exactly one block (508 bytes)")
    void exactlyOneBlock() {
        byte[] data = new byte[508];
        Arrays.fill(data, (byte) 'A');
        assertRoundtrip(data);
    }

    @Test
    @DisplayName("Multiple blocks (2000 bytes)")
    void multipleBlocks() {
        byte[] data = new byte[2000];
        for (int i = 0; i < data.length; i++) data[i] = (byte) (i % 256);
        assertRoundtrip(data);
    }

    // ---- Compression effectiveness ----

    @Test
    @DisplayName("Repeated data compresses significantly")
    void repeatedData() {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 'X');
        byte[] compressed = Picocompress.compress(data);
        assertTrue(compressed.length < data.length / 2,
            "Expected significant compression for repeated data, got "
                + compressed.length + " from " + data.length);
        assertArrayEquals(data, Picocompress.decompress(compressed));
    }

    // ---- Dictionary hits ----

    @Test
    @DisplayName("JSON data triggers dictionary matches")
    void jsonDictionary() {
        String json = """
            {"name":"test","type":"value","status":"active","code":"200",\
            "message":"content","request":"default","response":"result"}""";
        byte[] data = json.getBytes(StandardCharsets.US_ASCII);
        byte[] compressed = Picocompress.compress(data);
        assertTrue(compressed.length < data.length,
            "JSON should compress, got " + compressed.length + " >= " + data.length);
        assertRoundtrip(data);
    }

    @Test
    @DisplayName("Dictionary entries roundtrip correctly")
    void allDictEntries() {
        // Build input that contains every dictionary entry
        StringBuilder sb = new StringBuilder();
        String[] entries = {
            "\":\"", "},\n\"", "</div", "tion", "ment", "ness", "able", "ight",
            "\":\"", "</di", "=\"ht", "the", "ing", ",\",", "\":{", "\":[",
            "ion", "ent", "ter", "and", "/>\r\n", "\"},", "\"],", "have",
            "no\":", "true", "null", "name", "data", "time", "type", "mode",
            "http", "tion", "code", "size", "ment", "list", "item", "text",
            "false", "error", "value", "state", "alert", "input", "ation", "order",
            "status", "number", "active", "device", "region", "string", "result", "length",
            "message", "content", "request", "default", "number\":", "operator", "https://", "response",
            ". The ", ". It ", ". This ", ". A ", "HTTP", "JSON", "The ", "None",
            "ment", "ness", "able", "ight", "ation", "ould ",
            "\": \"", "\", \"",
            "DIM", "FOR", "END", "REL", "EACH", "LOAD", "SAVE", "CARD",
            "JUMP", "PRINT", "INPUT", "GOSUB", "STREAM", "RETURN", "SWITCH", "PROGRAM"
        };
        for (String e : entries) {
            sb.append(e).append(' ');
        }
        byte[] data = sb.toString().getBytes(StandardCharsets.US_ASCII);
        assertRoundtrip(data);
    }

    // ---- LZ matches ----

    @Test
    @DisplayName("LZ short-offset match")
    void lzShortOffset() {
        // Create data with repeated pattern within 511-byte range
        String pattern = "abcdefghij";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 20; i++) sb.append(pattern);
        byte[] data = sb.toString().getBytes(StandardCharsets.US_ASCII);
        assertRoundtrip(data);
    }

    @Test
    @DisplayName("Cross-block history match")
    void crossBlockHistory() {
        // Create data where block 2 references block 1 patterns
        byte[] data = new byte[1200];
        // Fill with a pattern that repeats across blocks
        byte[] pattern = "The quick brown fox jumps over the lazy dog. ".getBytes(StandardCharsets.US_ASCII);
        for (int i = 0; i < data.length; i++) {
            data[i] = pattern[i % pattern.length];
        }
        assertRoundtrip(data);
    }

    // ---- Repeat-offset ----

    @Test
    @DisplayName("Repeat-offset coding on structured data")
    void repeatOffset() {
        // Fixed-stride records: same offset recurs
        byte[] data = new byte[508];
        for (int i = 0; i < data.length; i++) {
            data[i] = (byte) ((i % 12 == 0) ? 0xAA : (i % 7));
        }
        assertRoundtrip(data);
    }

    // ---- Profile tests ----

    static Stream<Picocompress.Options> allProfiles() {
        return Stream.of(
            Picocompress.Options.MICRO,
            Picocompress.Options.MINIMAL,
            Picocompress.Options.BALANCED,
            Picocompress.Options.AGGRESSIVE,
            Picocompress.Options.Q3,
            Picocompress.Options.Q4
        );
    }

    @ParameterizedTest(name = "Profile roundtrip: {0}")
    @MethodSource("allProfiles")
    @DisplayName("All profiles roundtrip correctly")
    void profileRoundtrip(Picocompress.Options opts) {
        String text = """
            {"name":"sensor-1","type":"temperature","value":23.5,"status":"active",\
            "timestamp":"2024-01-15T10:30:00Z","device":"hub-A","region":"us-east",\
            "message":"reading normal","code":"200","result":"success"}""";
        byte[] data = text.getBytes(StandardCharsets.US_ASCII);
        byte[] big = new byte[data.length * 5];
        for (int i = 0; i < big.length; i++) big[i] = data[i % data.length];
        assertRoundtrip(big, opts);
    }

    // ---- Edge cases ----

    @Test
    @DisplayName("All-zero bytes")
    void allZeros() {
        assertRoundtrip(new byte[2048]);
    }

    @Test
    @DisplayName("All-0xFF bytes")
    void allOnes() {
        byte[] data = new byte[1024];
        Arrays.fill(data, (byte) 0xFF);
        assertRoundtrip(data);
    }

    @Test
    @DisplayName("Random data (incompressible)")
    void randomData() {
        Random rng = new Random(12345);
        byte[] data = new byte[3000];
        rng.nextBytes(data);
        byte[] compressed = Picocompress.compress(data);
        // Incompressible data: compressed ≈ raw + 4-byte headers per block
        assertArrayEquals(data, Picocompress.decompress(compressed));
    }

    @Test
    @DisplayName("Exactly 64 literal bytes (max single literal chunk)")
    void maxLiteralChunk() {
        byte[] data = new byte[64];
        for (int i = 0; i < 64; i++) data[i] = (byte) (i + 0x20);
        assertRoundtrip(data);
    }

    @Test
    @DisplayName("65 bytes forces two literal chunks")
    void twoLiteralChunks() {
        byte[] data = new byte[65];
        for (int i = 0; i < 65; i++) data[i] = (byte) (i + 0x20);
        assertRoundtrip(data);
    }

    @ParameterizedTest
    @ValueSource(ints = {1, 2, 3, 63, 64, 65, 100, 507, 508, 509, 1016, 2000, 5000})
    @DisplayName("Various input sizes roundtrip")
    void variousSizes(int size) {
        byte[] data = new byte[size];
        for (int i = 0; i < size; i++) data[i] = (byte) (i * 37 + 13);
        assertRoundtrip(data);
    }

    // ---- Binary data (dict skip) ----

    @Test
    @DisplayName("Binary data with non-printable bytes skips dictionary")
    void binaryData() {
        byte[] data = new byte[508];
        for (int i = 0; i < data.length; i++) data[i] = (byte) (i & 0xFF);
        assertRoundtrip(data);
    }

    // ---- Block boundary ----

    @Test
    @DisplayName("Data spanning exactly 3 blocks")
    void threeBlocks() {
        byte[] data = new byte[508 * 3];
        byte[] phrase = "Hello World! This is picocompress testing data. ".getBytes(StandardCharsets.US_ASCII);
        for (int i = 0; i < data.length; i++) data[i] = phrase[i % phrase.length];
        assertRoundtrip(data);
    }

    // ---- Error handling ----

    @Test
    @DisplayName("Null input throws NullPointerException")
    void nullInputCompress() {
        assertThrows(NullPointerException.class, () -> Picocompress.compress(null));
    }

    @Test
    @DisplayName("Null compressed input throws NullPointerException")
    void nullInputDecompress() {
        assertThrows(NullPointerException.class, () -> Picocompress.decompress(null));
    }

    @Test
    @DisplayName("Truncated compressed data throws")
    void truncatedData() {
        byte[] data = "Hello, world!".getBytes(StandardCharsets.US_ASCII);
        byte[] compressed = Picocompress.compress(data);
        // Truncate to just the header
        byte[] truncated = Arrays.copyOf(compressed, 4);
        assertThrows(IllegalArgumentException.class, () -> Picocompress.decompress(truncated));
    }

    @Test
    @DisplayName("Corrupt header throws")
    void corruptHeader() {
        // raw_len=100 but only 2 bytes of payload
        byte[] corrupt = {100, 0, 0, 0, 0x41, 0x42};
        assertThrows(IllegalArgumentException.class, () -> Picocompress.decompress(corrupt));
    }

    // ---- Determinism ----

    @Test
    @DisplayName("Compression is deterministic")
    void deterministic() {
        byte[] data = "Determinism check: same input must produce same output every time."
            .getBytes(StandardCharsets.US_ASCII);
        byte[] c1 = Picocompress.compress(data);
        byte[] c2 = Picocompress.compress(data);
        assertArrayEquals(c1, c2);
    }

    // ---- Large multi-block with prose (exercises cross-block + dict + LZ) ----

    @Test
    @DisplayName("Large prose text roundtrips with compression")
    void largeProse() {
        String text = """
            The quick brown fox jumps over the lazy dog. This sentence contains \
            every letter of the alphabet. The message was sent by the default \
            operator in the active region. The status of the request was a \
            successful response with the content length indicating the result \
            was complete. The input value of the alert state is false. The \
            device named "sensor-01" has type "temperature" and mode "active". \
            The order number is stored in the list item text. An error in the \
            code size might cause the time data to be null instead of true. \
            """;
        byte[] data = text.getBytes(StandardCharsets.US_ASCII);
        byte[] big = new byte[data.length * 4];
        for (int i = 0; i < big.length; i++) big[i] = data[i % data.length];
        byte[] compressed = Picocompress.compress(big);
        assertTrue(compressed.length < big.length,
            "Prose should compress: " + compressed.length + " >= " + big.length);
        assertArrayEquals(big, Picocompress.decompress(compressed));
    }

    // ---- MICRO profile with small block boundary ----

    @Test
    @DisplayName("MICRO profile with small input")
    void microSmallInput() {
        byte[] data = "Short text.".getBytes(StandardCharsets.US_ASCII);
        assertRoundtrip(data, Picocompress.Options.MICRO);
    }

    @Test
    @DisplayName("Q4 profile large cross-block history")
    void q4LargeHistory() {
        byte[] data = new byte[5000];
        byte[] pat = "abcdefghijklmnopqrstuvwxyz0123456789".getBytes(StandardCharsets.US_ASCII);
        for (int i = 0; i < data.length; i++) data[i] = pat[i % pat.length];
        assertRoundtrip(data, Picocompress.Options.Q4);
    }

    // ---- Custom Options validation ----

    @Test
    @DisplayName("Invalid blockSize throws")
    void invalidBlockSize() {
        assertThrows(IllegalArgumentException.class,
            () -> new Picocompress.Options(0, 9, 2, 504, 1));
        assertThrows(IllegalArgumentException.class,
            () -> new Picocompress.Options(512, 9, 2, 504, 1));
    }

    @Test
    @DisplayName("Invalid hashBits throws")
    void invalidHashBits() {
        assertThrows(IllegalArgumentException.class,
            () -> new Picocompress.Options(508, 0, 2, 504, 1));
    }

    @Test
    @DisplayName("Invalid hashChainDepth throws")
    void invalidDepth() {
        assertThrows(IllegalArgumentException.class,
            () -> new Picocompress.Options(508, 9, 0, 504, 1));
    }
}

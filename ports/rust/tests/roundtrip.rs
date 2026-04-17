use picocompress::{compress, compress_with, decompress, Error, Options};

#[test]
fn roundtrip_empty() {
    let c = compress(b"");
    let d = decompress(&c).unwrap();
    assert!(d.is_empty());
}

#[test]
fn roundtrip_short_string() {
    let data = b"Hello, world!";
    let c = compress(data);
    let d = decompress(&c).unwrap();
    assert_eq!(&d, data);
}

#[test]
fn roundtrip_repetitive_data() {
    let data = b"abcdefabcdefabcdefabcdefabcdefabcdefabcdef";
    let c = compress(data);
    let d = decompress(&c).unwrap();
    assert_eq!(&d[..], &data[..]);
    // Compressed should be smaller
    assert!(c.len() < data.len());
}

#[test]
fn roundtrip_json_payload() {
    let data = br#"{"name":"Alice","type":"string","status":"active","value":"content","message":"hello"}"#;
    let c = compress(data);
    let d = decompress(&c).unwrap();
    assert_eq!(&d[..], &data[..]);
}

#[test]
fn roundtrip_multi_block() {
    // Generate data larger than one block (508 bytes)
    let mut data = Vec::new();
    for i in 0..200u32 {
        data.extend_from_slice(
            format!("{{\"id\":{},\"name\":\"item_{:04}\",\"status\":\"active\"}},", i, i).as_bytes(),
        );
    }
    let c = compress(&data);
    let d = decompress(&c).unwrap();
    assert_eq!(d, data);
}

#[test]
fn roundtrip_all_byte_values() {
    let data: Vec<u8> = (0..=255u8).collect();
    let c = compress(&data);
    let d = decompress(&c).unwrap();
    assert_eq!(d, data);
}

#[test]
fn roundtrip_single_byte() {
    let data = b"X";
    let c = compress(data);
    let d = decompress(&c).unwrap();
    assert_eq!(&d, data);
}

#[test]
fn roundtrip_exactly_one_block() {
    let data = vec![b'A'; 508];
    let c = compress(&data);
    let d = decompress(&c).unwrap();
    assert_eq!(d, data);
}

#[test]
fn roundtrip_two_blocks_exact() {
    let data = vec![b'B'; 1016]; // 508 * 2
    let c = compress(&data);
    let d = decompress(&c).unwrap();
    assert_eq!(d, data);
}

#[test]
fn roundtrip_with_options() {
    let opts = Options::new()
        .block_size(256)
        .history_size(128)
        .hash_bits(8)
        .hash_chain_depth(1)
        .lazy_steps(0);
    let data = b"The quick brown fox jumps over the lazy dog. The quick brown fox jumps again.";
    let c = compress_with(data, &opts);
    let d = decompress(&c).unwrap();
    assert_eq!(&d[..], &data[..]);
}

#[test]
fn roundtrip_dictionary_words() {
    // Text containing many dictionary entries
    let data = b"true false null name data time type mode code size list item text \
                 error value state alert input order status number active device \
                 region string result length message content request default response";
    let c = compress(data);
    let d = decompress(&c).unwrap();
    assert_eq!(&d[..], &data[..]);
}

#[test]
fn roundtrip_large_repetitive() {
    let pattern = b"the quick brown fox ";
    let mut data = Vec::new();
    for _ in 0..500 {
        data.extend_from_slice(pattern);
    }
    let c = compress(&data);
    let d = decompress(&c).unwrap();
    assert_eq!(d, data);
    // Should achieve meaningful compression
    assert!(c.len() < data.len() / 2);
}

#[test]
fn decompress_empty_input() {
    let d = decompress(b"").unwrap();
    assert!(d.is_empty());
}

#[test]
fn decompress_corrupt_header() {
    // raw_len=0xFFFF (>508), comp_len=1 → should fail
    assert!(matches!(
        decompress(&[0xFF, 0xFF, 0x01, 0x00, 0x00]),
        Err(Error::Corrupt)
    ));
}

#[test]
fn decompress_truncated() {
    // Valid header but truncated payload
    let data = b"hello world hello world";
    let c = compress(data);
    let truncated = &c[..c.len() / 2];
    assert!(decompress(truncated).is_err());
}

#[test]
fn compression_actually_compresses() {
    let data = b"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    let c = compress(data);
    // 4-byte header + compressed payload should be smaller than raw
    assert!(c.len() < data.len());
}

#[test]
fn roundtrip_html_like() {
    let data = b"<div><div></div></div><div></div>";
    let c = compress(data);
    let d = decompress(&c).unwrap();
    assert_eq!(&d[..], &data[..]);
}

#[test]
fn roundtrip_keywords_uppercase() {
    let data = b"DIM FOR END PRINT INPUT GOSUB RETURN SWITCH PROGRAM STREAM LOAD SAVE";
    let c = compress(data);
    let d = decompress(&c).unwrap();
    assert_eq!(&d[..], &data[..]);
}

#[test]
fn roundtrip_stress_5kb() {
    let mut data = Vec::new();
    for i in 0u32..5000 {
        data.push((i % 256) as u8);
        if i % 50 == 0 {
            data.extend_from_slice(b"status");
        }
    }
    let c = compress(&data);
    let d = decompress(&c).unwrap();
    assert_eq!(d, data);
}

#[test]
fn options_default_matches_compress() {
    let data = b"test default options produce same output as compress()";
    let c1 = compress(data);
    let c2 = compress_with(data, &Options::default());
    assert_eq!(c1, c2);
}

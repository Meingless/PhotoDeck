#![no_main]
use libfuzzer_sys::fuzz_target;

fuzz_target!(|bytes: &[u8]| {
    // Bound the harness input independently of the production parser's 384 MiB
    // file cap. Tiny malformed headers can still exercise decoder memory limits.
    if bytes.len() > 1024 * 1024 {
        return;
    }
    if let Ok((state, _)) = photodeck::project::decode_state(bytes) {
        photodeck::document::validate_state(&state).expect("decoder returned invalid state");
        let encoded = photodeck::project::encode_state(&state, true)
            .expect("valid parsed state must be encodable");
        let (roundtrip, _) = photodeck::project::decode_state(&encoded)
            .expect("writer must produce readable recovery state");
        assert_eq!(state, roundtrip);
    }
});

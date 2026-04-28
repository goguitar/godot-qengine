//! Lock-free SPSC ring buffer for passing audio samples from the audio thread
//! to the main thread.
//!
//! Uses [`rtrb`] which is explicitly designed for real-time audio scenarios
//! (never allocates, never blocks, no system calls on the hot path).

use rtrb::{Consumer, Producer, RingBuffer};

/// Default capacity: ~1 second at 44 100 Hz stereo → 88 200 stereo frames.
/// We store interleaved mono samples so 131 072 gives a comfortable margin.
pub const DEFAULT_CAPACITY: usize = 1 << 17; // 131 072

/// Convenience wrapper that owns both ends of the ring buffer.
///
/// After construction, split it with [`AudioRingBuffer::split`] and send
/// the producer to the audio thread.
pub struct AudioRingBuffer {
    producer: Option<Producer<f32>>,
    consumer: Option<Consumer<f32>>,
}

impl AudioRingBuffer {
    /// Create a new ring buffer with the given sample capacity.
    pub fn new(capacity: usize) -> Self {
        let (producer, consumer) = RingBuffer::new(capacity);
        AudioRingBuffer {
            producer: Some(producer),
            consumer: Some(consumer),
        }
    }

    /// Split into `(Producer, Consumer)`.  Each end can be sent to a
    /// different thread.  Panics if called more than once.
    pub fn split(&mut self) -> (Producer<f32>, Consumer<f32>) {
        (
            self.producer.take().expect("producer already taken"),
            self.consumer.take().expect("consumer already taken"),
        )
    }
}

/// Extension trait for pushing slices into a `Producer<f32>`.
pub trait ProducerExt {
    /// Push as many samples as possible.  Returns the number actually pushed.
    fn push_samples(&mut self, samples: &[f32]) -> usize;
}

impl ProducerExt for Producer<f32> {
    fn push_samples(&mut self, samples: &[f32]) -> usize {
        let slots = self.slots();
        let count = samples.len().min(slots);
        for &s in &samples[..count] {
            // Cannot fail: we checked `slots` above and this is the only producer.
            let _ = self.push(s);
        }
        count
    }
}

/// Extension trait for draining a `Consumer<f32>` into a `Vec`.
pub trait ConsumerExt {
    /// Drain all available samples into `out`.
    fn drain_into(&mut self, out: &mut Vec<f32>);
}

impl ConsumerExt for Consumer<f32> {
    fn drain_into(&mut self, out: &mut Vec<f32>) {
        let available = self.slots();
        out.reserve(available);
        for _ in 0..available {
            match self.pop() {
                Ok(s)  => out.push(s),
                Err(_) => break,
            }
        }
    }
}

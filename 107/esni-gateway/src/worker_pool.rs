use crossbeam_queue::ArrayQueue;
use parking_lot::Mutex;
use std::sync::Arc;
use std::thread;
use tokio::runtime::{Handle, Runtime};

pub struct WorkerPool {
    workers: Vec<Worker>,
    task_queue: Arc<ArrayQueue<Task>>,
}

type Task = Box<dyn FnOnce() + Send + 'static>;

struct Worker {
    handle: Option<thread::JoinHandle<()>>,
}

impl Worker {
    fn new(id: usize, task_queue: Arc<ArrayQueue<Task>>) -> Self {
        let handle = thread::spawn(move || {
            loop {
                if let Some(task) = task_queue.pop() {
                    task();
                } else {
                    thread::yield_now();
                }
            }
        });

        Self {
            handle: Some(handle),
        }
    }
}

impl WorkerPool {
    pub fn new(num_workers: usize, queue_size: usize) -> Self {
        let task_queue = Arc::new(ArrayQueue::new(queue_size));
        let mut workers = Vec::with_capacity(num_workers);

        for id in 0..num_workers {
            workers.push(Worker::new(id, Arc::clone(&task_queue)));
        }

        Self { workers, task_queue }
    }

    pub fn submit<F>(&self, task: F) -> Result<(), Task>
    where
        F: FnOnce() + Send + 'static,
    {
        self.task_queue.push(Box::new(task))
    }
}

pub struct TokioWorkerPool {
    runtimes: Vec<Runtime>,
    current: Mutex<usize>,
}

impl TokioWorkerPool {
    pub fn new(num_workers: usize) -> Self {
        let mut runtimes = Vec::with_capacity(num_workers);

        for _ in 0..num_workers {
            let runtime = tokio::runtime::Builder::new_current_thread()
                .enable_all()
                .build()
                .expect("Failed to create tokio runtime");
            runtimes.push(runtime);
        }

        Self {
            runtimes,
            current: Mutex::new(0),
        }
    }

    pub fn spawn<F>(&self, future: F)
    where
        F: std::future::Future + Send + 'static,
        F::Output: Send + 'static,
    {
        let mut current = self.current.lock();
        let idx = *current % self.runtimes.len();
        *current += 1;
        drop(current);

        self.runtimes[idx].spawn(future);
    }

    pub fn handle(&self) -> Handle {
        self.runtimes[0].handle().clone()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_worker_pool_creation() {
        let pool = WorkerPool::new(4, 1000);
        assert_eq!(pool.workers.len(), 4);
    }

    #[test]
    fn test_tokio_worker_pool_creation() {
        let pool = TokioWorkerPool::new(4);
        assert_eq!(pool.runtimes.len(), 4);
    }
}

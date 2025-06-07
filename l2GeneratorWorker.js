// Web Worker – L2 Generator running in background
// This mirrors the SimpleL2Generator logic but without any DOM dependencies.

class SimpleL2Generator {
    constructor() {
        this.midPrice = 100.0;
        this.isRunning = false;
        this.interval = null;
        this.eventCount = 0;
        this.rate = 10; // events per second default
        this.currentBids = [];
        this.currentAsks = [];
        this.initializeBook();
    }

    initializeBook() {
        this.currentBids = [];
        this.currentAsks = [];
        for (let i = 0; i < 10; i++) {
            this.currentBids.push({
                price: this.midPrice - 0.01 - i * 0.01,
                size: Math.random() * 50 + 10
            });
            this.currentAsks.push({
                price: this.midPrice + 0.01 + i * 0.01,
                size: Math.random() * 50 + 10
            });
        }
    }

    start() {
        if (this.isRunning) return;
        this.isRunning = true;
        this.interval = setInterval(() => this.generateEvent(), 1000 / this.rate);
    }

    stop() {
        if (!this.isRunning) return;
        this.isRunning = false;
        if (this.interval) {
            clearInterval(this.interval);
            this.interval = null;
        }
    }

    reset() {
        this.stop();
        this.eventCount = 0;
        this.initializeBook();
    }

    generateEvent() {
        // Randomly modify book
        const eventType = Math.random();
        if (eventType < 0.3) {
            const idx = Math.floor(Math.random() * this.currentBids.length);
            this.currentBids[idx].size = Math.random() * 80 + 5;
        } else if (eventType < 0.6) {
            const idx = Math.floor(Math.random() * this.currentAsks.length);
            this.currentAsks[idx].size = Math.random() * 80 + 5;
        } else if (eventType < 0.8) {
            if (Math.random() < 0.5 && this.currentBids.length > 5) {
                this.currentBids.pop();
            } else if (this.currentBids.length < 15) {
                this.currentBids.push({
                    price: this.midPrice - 0.01 - this.currentBids.length * 0.01,
                    size: Math.random() * 50 + 10
                });
            }
        } else {
            if (Math.random() < 0.5 && this.currentAsks.length > 5) {
                this.currentAsks.pop();
            } else if (this.currentAsks.length < 15) {
                this.currentAsks.push({
                    price: this.midPrice + 0.01 + this.currentAsks.length * 0.01,
                    size: Math.random() * 50 + 10
                });
            }
        }

        this.eventCount++;

        // Flatten book into Float32Array [price, size, sideFlag]
        const levels = [...this.currentBids, ...this.currentAsks];
        const buffer = new Float32Array(levels.length * 3);
        let idx = 0;
        for (const level of this.currentBids) {
            buffer[idx++] = level.price;
            buffer[idx++] = level.size;
            buffer[idx++] = 0; // bid flag
        }
        for (const level of this.currentAsks) {
            buffer[idx++] = level.price;
            buffer[idx++] = level.size;
            buffer[idx++] = 1; // ask flag
        }

        self.postMessage({
            type: 'snapshot',
            timestamp: performance.now(),
            buffer: buffer.buffer,
            count: levels.length
        }, [buffer.buffer]);
    }

    setRate(newRate) {
        this.rate = Math.max(1, newRate);
        if (this.isRunning) {
            clearInterval(this.interval);
            this.interval = setInterval(() => this.generateEvent(), 1000 / this.rate);
        }
    }
}

const generator = new SimpleL2Generator();

self.onmessage = (e) => {
    const { type } = e.data;
    switch (type) {
        case 'start':
            generator.start();
            break;
        case 'stop':
            generator.stop();
            break;
        case 'reset':
            generator.reset();
            break;
        case 'getStats':
            self.postMessage({
                type: 'stats',
                stats: {
                    events: generator.eventCount,
                    isRunning: generator.isRunning
                }
            });
            break;
        case 'setRate':
            generator.setRate(e.data.rate);
            break;
    }
}; 
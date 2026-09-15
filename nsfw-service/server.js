import sharp from 'sharp';
import multer from 'multer';
import path from 'node:path';
import express from 'express';
import * as nsfwjs from 'nsfwjs';
import * as tf from '@tensorflow/tfjs';
import { createRequire } from 'node:module';
import { setWasmPaths } from '@tensorflow/tfjs-backend-wasm';

const PORT = Number(process.env.PORT || 8081);
const MAX_BYTES = 2 * 1024 * 1024;
const THRESHOLDS = {
	Porn: Number(process.env.NSFW_THRESHOLD_PORN || 0.6),
	Hentai: Number(process.env.NSFW_THRESHOLD_HENTAI || 0.6),
	Sexy: Number(process.env.NSFW_THRESHOLD_SEXY || 0.8),
};

const upload = multer({
	storage: multer.memoryStorage(),
	limits: { fileSize: MAX_BYTES },
});

let model = null;
let ready = false;
let classifyQueue = Promise.resolve();

function scoresFromPredictions(predictions) {
	const scores = { Drawing: 0, Hentai: 0, Neutral: 0, Porn: 0, Sexy: 0 };
	for (const item of predictions) {
		if (Object.prototype.hasOwnProperty.call(scores, item.className)) {
			scores[item.className] = item.probability;
		}
	}
	return scores;
}

function isNsfw(scores) {
	return scores.Porn >= THRESHOLDS.Porn || scores.Hentai >= THRESHOLDS.Hentai || scores.Sexy >= THRESHOLDS.Sexy;
}

async function imageToTensor(buffer) {
	const { data, info } = await sharp(buffer, { animated: true, pages: 1 })
		.rotate()
		.resize(512, 512, { fit: 'inside', withoutEnlargement: true })
		.removeAlpha()
		.raw()
		.toBuffer({ resolveWithObject: true });

	if (info.channels !== 3) {
		throw new Error('expected rgb image');
	}

	return tf.tensor3d(new Uint8Array(data), [info.height, info.width, 3]);
}

function classifySerialized(fn) {
	const run = classifyQueue.then(fn, fn);
	classifyQueue = run.catch(() => undefined);
	return run;
}

async function initBackend() {
	try {
		const require = createRequire(import.meta.url);
		const wasmDir = path.join(path.dirname(require.resolve('@tensorflow/tfjs-backend-wasm/package.json')), 'wasm-out') + path.sep;
		setWasmPaths(wasmDir);
		const ok = await tf.setBackend('wasm');
		if (!ok) {
			throw new Error('wasm backend unavailable');
		}
	} catch (err) {
		console.warn('wasm backend failed, falling back to cpu:', err.message);
		await tf.setBackend('cpu');
	}

	await tf.ready();
	console.log(`tfjs backend: ${tf.getBackend()}`);
}

const app = express();
app.disable('x-powered-by');

app.get('/health', (_req, res) => {
	if (!ready) {
		return res.status(503).json({ ok: false });
	}

	return res.json({ ok: true });
});

app.post('/moderate', (req, res, next) => {
	upload.single('image')(req, res, (err) => {
		if (!err) {
			next();
			return;
		}

		if (err.code === 'LIMIT_FILE_SIZE') {
			res.status(413).json({ error: 'image too large' });
			return;
		}

		res.status(400).json({ error: 'invalid image' });
	});
}, async (req, res) => {
	if (!ready || !model) {
		res.status(503).json({ error: 'model not ready' });
		return;
	}

	if (!req.file?.buffer?.length) {
		res.status(400).json({ error: 'missing image' });
		return;
	}

	try {
		const result = await classifySerialized(async () => {
			const image = await imageToTensor(req.file.buffer);
			try {
				const predictions = await model.classify(image);
				return scoresFromPredictions(predictions);
			} finally {
				image.dispose();
			}
		});

		res.json({ nsfw: isNsfw(result), scores: result });
	} catch (err) {
		if (err?.message === 'expected rgb image' || /unsupported image|Input file is missing|corrupt/i.test(String(err))) {
			res.status(400).json({ error: 'invalid image' });
			return;
		}

		console.error('nsfw classify failed:', err);
		res.status(500).json({ error: 'classify failed' });
	}
});

async function main() {
	app.listen(PORT, '0.0.0.0', () => {
		console.log(`nsfw service listening on ${PORT}`);
	});

	try {
		await initBackend();
		model = await nsfwjs.load();
		ready = true;
		console.log('nsfw model ready');
	} catch (err) {
		console.error('failed to load nsfw model:', err);
		process.exit(1);
	}
}

main();

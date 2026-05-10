import express from 'express';
import dotenv from 'dotenv';
import path from 'path';
import { fileURLToPath } from 'url';
import './db.js';
import streaksRouter from './routes/streaks.js';
import authRouter from './routes/auth.js';
import healthRouter from './routes/health.js';
import metricsRouter from './routes/metrics.js';

dotenv.config();

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.json());
app.use(express.static(path.join(__dirname, '../public')));

app.use('/api', authRouter);
app.use('/api/streaks', streaksRouter);
app.use('/health', healthRouter);
app.use('/metrics', metricsRouter);

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

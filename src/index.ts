import express from 'express';
import dotenv from 'dotenv';
import './db.js';
import streaksRouter from './routes/streaks.js';
import authRouter from './routes/auth.js';
import healthRouter from './routes/health.js';
import metricsRouter from './routes/metrics.js';

dotenv.config();

const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.json());

app.use('/api', authRouter);
app.use('/api/streaks', streaksRouter);
app.use('/health', healthRouter);
app.use('/metrics', metricsRouter);

app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});

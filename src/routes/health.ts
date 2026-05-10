import { Router, type Request, type Response, type Router as ExpressRouter } from 'express';
import { redis } from '../db.js';

const router: ExpressRouter = Router();

router.get('/', async (req: Request, res: Response) => {
  try {
    const ping = await redis.ping();
    if (ping === 'PONG') {
      res.json({ status: 'ok', redis: 'connected' });
    } else {
      res.status(503).json({ status: 'error', redis: 'unexpected response' });
    }
  } catch (error) {
    res.status(503).json({ status: 'error', redis: 'disconnected' });
  }
});

export default router;

import type { Request, Response, NextFunction } from 'express';
import axios from 'axios';
import { redis } from '../db.js';

export interface AuthRequest extends Request {
  user?: {
    id: string;
  };
}

export const requireAuth = async (req: AuthRequest, res: Response, next: NextFunction) => {
  const authHeader = req.headers.authorization;
  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    res.status(401).json({ error: 'Unauthorized: Invalid token format' });
    return;
  }

  const token = authHeader.slice(7);

  try {
    const response = await axios.get('https://discord.com/api/v10/users/@me', {
      headers: {
        Authorization: `Bearer ${token}`,
      },
    });

    if (response.data && response.data.id) {
      req.user = { id: response.data.id };

      const rateLimitKey = `ratelimit:${req.user.id}`;
      const current = await redis.incr(rateLimitKey);
      if (current === 1) {
        await redis.expire(rateLimitKey, 60);
      }
      if (current > 50) {
        res.status(429).json({ error: 'Too many requests' });
        return;
      }

      next();
    } else {
      res.status(401).json({ error: 'Unauthorized: Invalid token response' });
    }
  } catch (error) {
    res.status(401).json({ error: 'Unauthorized: Invalid token' });
  }
};

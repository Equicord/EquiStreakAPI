import type { Request, Response, NextFunction } from 'express';
import axios from 'axios';

export interface AuthRequest extends Request {
  user?: {
    id: string;
  };
}

export const requireAuth = async (req: AuthRequest, res: Response, next: NextFunction) => {
  const authHeader = req.headers.authorization;
  if (!authHeader) {
    res.status(401).json({ error: 'Unauthorized: No token provided' });
    return;
  }

  try {
    const response = await axios.get('https://discord.com/api/v10/users/@me', {
      headers: {
        Authorization: authHeader,
      },
    });

    if (response.data && response.data.id) {
      req.user = { id: response.data.id };
      next();
    } else {
      res.status(401).json({ error: 'Unauthorized: Invalid token response' });
    }
  } catch (error) {
    res.status(401).json({ error: 'Unauthorized: Invalid token' });
  }
};

import type { Request, Response, NextFunction } from 'express';

export const requireAdmin = (req: Request, res: Response, next: NextFunction) => {
  const apiKey = req.headers['x-api-key'];
  const masterKey = process.env.MASTER_API_KEY;

  if (!masterKey) {
    res.status(500).json({ error: 'Server misconfiguration: MASTER_API_KEY not set' });
    return;
  }

  if (apiKey !== masterKey) {
    res.status(403).json({ error: 'Forbidden: Invalid API Key' });
    return;
  }

  next();
};

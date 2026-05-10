import { Router } from 'express';
import type { Request, Response, Router as ExpressRouter } from 'express';
import axios from 'axios';

const router: ExpressRouter = Router();

// GET /api/authorize
router.get('/authorize', async (req: Request, res: Response) => {
  const code = req.query.code as string;

  if (!code) {
    res.status(400).send('Missing code parameter');
    return;
  }

  const clientId = process.env.DISCORD_CLIENT_ID;
  const clientSecret = process.env.DISCORD_CLIENT_SECRET;
  const redirectUri = process.env.DISCORD_REDIRECT_URI || 'http://localhost:3000/api/authorize';

  if (!clientId || !clientSecret) {
    res.status(500).send('Server misconfiguration: Missing Discord OAuth credentials');
    return;
  }

  try {
    const params = new URLSearchParams({
      client_id: clientId,
      client_secret: clientSecret,
      grant_type: 'authorization_code',
      code: code,
      redirect_uri: redirectUri,
    });

    const response = await axios.post('https://discord.com/api/oauth2/token', params.toString(), {
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded'
      }
    });

    const data = response.data;
    if (data.access_token) {
      res.send(data.access_token);
    } else {
      res.status(400).send('Failed to obtain access token');
    }
  } catch (error: any) {
    console.error('OAuth2 exchange error:', error.response?.data || error.message);
    res.status(500).send('OAuth2 exchange failed');
  }
});

export default router;

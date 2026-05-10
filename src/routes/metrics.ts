import { Router, type Request, type Response, type Router as ExpressRouter } from 'express';
import { redis } from '../db.js';

const router: ExpressRouter = Router();

router.get('/', async (req: Request, res: Response) => {
  try {
    const userKeys = await redis.keys('user_streaks:*');
    const streakKeys = await redis.keys('streak:*');

    let streaksDay = 0;
    let streaksWeek = 0;
    let streaksMonth = 0;

    const usersDay = new Set<string>();
    const usersWeek = new Set<string>();
    const usersMonth = new Set<string>();

    if (streakKeys.length > 0) {
      const pipeline = redis.pipeline();
      streakKeys.forEach(k => pipeline.hmget(k, 'last_streak_date', 'user_a_id', 'user_b_id', 'today_date'));
      const results = await pipeline.exec();

      const now = new Date();
      now.setHours(0, 0, 0, 0);

      if (results) {
        results.forEach(([err, data]) => {
          if (err || !data) return;
          const [last_streak_date, user_a_id, user_b_id, today_date] = data as string[];

          const activeDateStr = today_date || last_streak_date;
          if (!activeDateStr) return;

          const activeDate = new Date(activeDateStr);
          activeDate.setHours(0, 0, 0, 0);

          const diffDays = Math.floor((now.getTime() - activeDate.getTime()) / (1000 * 60 * 60 * 24));

          if (diffDays <= 1) {
            streaksDay++;
            if (user_a_id) usersDay.add(user_a_id);
            if (user_b_id) usersDay.add(user_b_id);
          }
          if (diffDays <= 7) {
            streaksWeek++;
            if (user_a_id) usersWeek.add(user_a_id);
            if (user_b_id) usersWeek.add(user_b_id);
          }
          if (diffDays <= 30) {
            streaksMonth++;
            if (user_a_id) usersMonth.add(user_a_id);
            if (user_b_id) usersMonth.add(user_b_id);
          }
        });
      }
    }

    res.json({
      timestamp: Math.floor(Date.now() / 1000),
      uptime_seconds: Math.floor(process.uptime()),
      users_day: usersDay.size,
      users_week: usersWeek.size,
      users_month: usersMonth.size,
      users_total: userKeys.length,
      streaks_day: streaksDay,
      streaks_week: streaksWeek,
      streaks_month: streaksMonth,
      streaks_total: streakKeys.length
    });
  } catch (error) {
    console.error(error);
    res.status(500).json({ error: 'Internal server error' });
  }
});

export default router;

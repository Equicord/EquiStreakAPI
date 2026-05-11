import { Router } from 'express';
import type { Response, Router as ExpressRouter } from 'express';
import { redis } from '../db.js';
import { requireAuth } from '../middleware/auth.js';
import type { AuthRequest } from '../middleware/auth.js';
import { requireAdmin } from '../middleware/admin.js';

const router: ExpressRouter = Router();

// POST /api/streaks/admin/update
router.post('/admin/update', requireAdmin as any, async (req: any, res: Response) => {
  const { user_a_id, user_b_id, count, last_streak_date, today_date, user_a_today, user_b_today } = req.body;
  if (!user_a_id || !user_b_id || count === undefined) {
    res.status(400).json({ error: 'Missing required fields: user_a_id, user_b_id, count' });
    return;
  }

  const today = new Date().toISOString().split('T')[0];
  const [idA, idB] = [user_a_id, user_b_id].sort();
  const streakKey = `streak:${idA}:${idB}`;

  try {
    await redis.pipeline()
      .sadd(`user_streaks:${idA}`, streakKey)
      .sadd(`user_streaks:${idB}`, streakKey)
      .exec();

    await redis.hset(streakKey, {
      count: String(count),
      user_a_id: idA,
      user_b_id: idB,
      last_streak_date: last_streak_date ?? today,
      today_date: today_date ?? today,
      user_a_today: user_a_today ? "1" : "0",
      user_b_today: user_b_today ? "1" : "0",
    });

    const obj = await redis.hgetall(streakKey);
    res.json({
      id: `${obj.user_a_id}:${obj.user_b_id}`,
      user_a_id: obj.user_a_id,
      user_b_id: obj.user_b_id,
      count: Number(obj.count) || 0,
      last_streak_date: obj.last_streak_date || null,
      user_a_today: obj.user_a_today === '1',
      user_b_today: obj.user_b_today === '1',
      today_date: obj.today_date
    });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Internal server error' });
  }
});

router.use(requireAuth as any);

const getTodayDateString = () => {
  return new Date().toISOString().split('T')[0];
};

const getYesterdayDateString = () => {
  const date = new Date();
  date.setDate(date.getDate() - 1);
  return date.toISOString().split('T')[0];
};

const updateStreakLua = `
local streak_key = KEYS[1]
local current_user = ARGV[1]
local user_a_id = ARGV[2]
local user_b_id = ARGV[3]
local today = ARGV[4]
local yesterday = ARGV[5]

local streak = redis.call('HMGET', streak_key, 'count', 'last_streak_date', 'user_a_today', 'user_b_today', 'today_date')
local count = tonumber(streak[1]) or 0
local last_streak_date = streak[2]
local user_a_today = streak[3] or "0"
local user_b_today = streak[4] or "0"
local today_date = streak[5]

if today_date ~= today then
  if today_date == yesterday then
    if user_a_today ~= "1" or user_b_today ~= "1" then
      count = 0
    end
  elseif today_date ~= false and today_date ~= nil then
    count = 0
  end
  
  user_a_today = "0"
  user_b_today = "0"
  today_date = today
end

local is_user_a = (current_user == user_a_id)

if is_user_a and user_a_today == "1" then
  return redis.call('HGETALL', streak_key)
elseif (not is_user_a) and user_b_today == "1" then
  return redis.call('HGETALL', streak_key)
end

if is_user_a then
  user_a_today = "1"
else
  user_b_today = "1"
end

if user_a_today == "1" and user_b_today == "1" and last_streak_date ~= today then
  count = count + 1
  last_streak_date = today
end

redis.call('HMSET', streak_key,
  'count', tostring(count),
  'last_streak_date', last_streak_date or "",
  'user_a_today', user_a_today,
  'user_b_today', user_b_today,
  'today_date', today_date,
  'user_a_id', user_a_id,
  'user_b_id', user_b_id
)

return redis.call('HGETALL', streak_key)
`;

redis.defineCommand('updateStreak', {
  numberOfKeys: 1,
  lua: updateStreakLua,
});

const parseHGetAll = (arr: any[]) => {
  if (!arr || arr.length === 0) return null;
  const obj: any = {};
  for (let i = 0; i < arr.length; i += 2) {
    let val = arr[i + 1];
    if (val === '0') val = false;
    else if (val === '1') val = true;
    else if (val === '') val = null;
    else if (!isNaN(Number(val))) val = Number(val);
    obj[arr[i]] = val;
  }

  obj.id = `${obj.user_a_id}:${obj.user_b_id}`;
  return obj;
};

// GET /api/streaks
router.get('/', async (req: AuthRequest, res: Response) => {
  const userId = req.user!.id;
  try {
    const streakIds = await redis.smembers(`user_streaks:${userId}`);
    if (streakIds.length === 0) {
      res.json([]);
      return;
    }

    const pipeline = redis.pipeline();
    streakIds.forEach((id: string) => {
      pipeline.hgetall(id);
    });

    const results = await pipeline.exec();

    const streaks = results?.map(([err, data]: [any, any]) => {
      if (err) return null;
      const obj = data as Record<string, string>;
      if (Object.keys(obj).length === 0) return null;

      return {
        id: `${obj.user_a_id}:${obj.user_b_id}`,
        user_a_id: obj.user_a_id,
        user_b_id: obj.user_b_id,
        count: Number(obj.count) || 0,
        last_streak_date: obj.last_streak_date || null,
        user_a_today: obj.user_a_today === '1',
        user_b_today: obj.user_b_today === '1',
        today_date: obj.today_date
      };
    }).filter((s: any) => s !== null) || [];

    res.json(streaks);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Internal server error' });
  }
});

// POST /api/streaks/migrate
router.post('/migrate', async (req: AuthRequest, res: Response) => {
  const userId = req.user!.id;
  const legacyStreaks = req.body;

  if (!legacyStreaks || typeof legacyStreaks !== 'object') {
    res.status(400).json({ error: 'Invalid payload' });
    return;
  }

  try {
    const pipeline = redis.pipeline();
    let migratedAny = false;

    for (const [recipientId, data] of Object.entries(legacyStreaks)) {
      const legacyData = data as any;
      if (!legacyData || typeof legacyData.count !== 'number') continue;

      const [idA, idB] = [userId, recipientId].sort();
      const streakKey = `streak:${idA}:${idB}`;

      const existingCountStr = await redis.hget(streakKey, 'count');
      const existingCount = Number(existingCountStr) || 0;

      const migratedCount = Math.min(50, Math.max(existingCount, legacyData.count));

      if (migratedCount > existingCount) {
        pipeline.sadd(`user_streaks:${idA}`, streakKey);
        pipeline.sadd(`user_streaks:${idB}`, streakKey);

        const isUserA = (userId === idA);
        const userAToday = isUserA && (legacyData.todayFlags & 1) ? "1" : "0";
        const userBToday = (!isUserA) && (legacyData.todayFlags & 1) ? "1" : "0";

        pipeline.hset(streakKey, {
          count: String(migratedCount),
          user_a_id: idA,
          user_b_id: idB,
          user_a_today: userAToday,
          user_b_today: userBToday,
          last_streak_date: legacyData.lastDay || "",
          today_date: legacyData.todayDate || ""
        });
        migratedAny = true;
      }
    }

    if (migratedAny) {
      await pipeline.exec();
    }

    res.json({ success: true });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Internal server error' });
  }
});

// GET /api/streaks/:recipient_id
router.get('/:recipient_id', async (req: AuthRequest, res: Response) => {
  const userId = req.user!.id;
  const recipientId = req.params.recipient_id;
  const [user_a_id, user_b_id] = [userId, recipientId].sort();
  const streakKey = `streak:${user_a_id}:${user_b_id}`;

  try {
    const obj = await redis.hgetall(streakKey);
    if (Object.keys(obj).length === 0) {
      res.status(404).json({ error: 'Streak not found' });
      return;
    }

    res.json({
      id: `${obj.user_a_id}:${obj.user_b_id}`,
      user_a_id: obj.user_a_id,
      user_b_id: obj.user_b_id,
      count: Number(obj.count) || 0,
      last_streak_date: obj.last_streak_date || null,
      user_a_today: obj.user_a_today === '1',
      user_b_today: obj.user_b_today === '1',
      today_date: obj.today_date
    });
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Internal server error' });
  }
});

// POST /api/streaks/:recipient_id
router.post('/:recipient_id', async (req: AuthRequest, res: Response) => {
  const userId = req.user!.id;
  const recipientId = req.params.recipient_id;

  if (userId === recipientId) {
    res.status(400).json({ error: 'Cannot streak with yourself' });
    return;
  }

  const [user_a_id, user_b_id] = [userId, recipientId].sort();
  const streakKey = `streak:${user_a_id}:${user_b_id}`;

  const today = getTodayDateString();
  const yesterday = getYesterdayDateString();

  try {
    await redis.pipeline()
      .sadd(`user_streaks:${user_a_id}`, streakKey)
      .sadd(`user_streaks:${user_b_id}`, streakKey)
      .exec();

    const resultArr = await (redis as any).updateStreak(
      1, streakKey,
      userId, user_a_id, user_b_id, today, yesterday
    );

    const obj = parseHGetAll(resultArr);
    res.json(obj);
  } catch (err) {
    console.error(err);
    res.status(500).json({ error: 'Internal server error' });
  }
});

export default router;

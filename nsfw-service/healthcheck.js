const port = process.env.PORT || 9090;

const res = await fetch(`http://127.0.0.1:${port}/health`);

process.exit(res.ok ? 0 : 1);

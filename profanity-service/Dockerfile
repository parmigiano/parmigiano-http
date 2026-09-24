FROM python:3.12-slim-bookworm

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY server.py healthcheck.py ./

ENV PORT=9191
EXPOSE 9191

CMD ["python", "server.py"]

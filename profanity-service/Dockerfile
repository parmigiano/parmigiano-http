FROM python:3.12-slim-bookworm

WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY server.py healthcheck.py ./

ENV PORT=8082
EXPOSE 8082

CMD ["python", "server.py"]

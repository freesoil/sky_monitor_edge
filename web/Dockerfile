FROM python:3.10-slim

WORKDIR /app

COPY requirements.txt .

RUN pip install \
    --index-url http://mirrors.cloud.aliyuncs.com/pypi/simple/ \
    --trusted-host mirrors.cloud.aliyuncs.com \
    seaborn torch torchvision flask opencv-python pandas

RUN pip install \
    --index-url http://mirrors.cloud.aliyuncs.com/pypi/simple/ \
    --trusted-host mirrors.cloud.aliyuncs.com \
    -r requirements.txt

COPY app/ app/
COPY templates/ templates/

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000", "--reload"]

from fastapi import FastAPI
from app.upload import router as upload_router
from app.dashboard import router as dashboard_router

app = FastAPI()
app.include_router(upload_router)
app.include_router(dashboard_router)

import os, json
from fastapi import APIRouter, Request
from fastapi.responses import HTMLResponse
from fastapi.templating import Jinja2Templates

router = APIRouter()
templates = Jinja2Templates(directory="templates")
RESULT_FOLDER = "shared/results"

@router.get("/dashboard", response_class=HTMLResponse)
async def dashboard(request: Request):
    os.makedirs(RESULT_FOLDER, exist_ok=True)
    files = [f for f in os.listdir(RESULT_FOLDER) if f.endswith(".json")]

    results = []
    for f in files:
        with open(os.path.join(RESULT_FOLDER, f)) as j:
            results.append((f, json.load(j)))

    return templates.TemplateResponse("dashboard.html", {
        "request": request,
        "results": results
    })

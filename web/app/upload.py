import os
import shutil
import requests
from fastapi import APIRouter, File, UploadFile, Request, HTTPException
from fastapi.responses import HTMLResponse

UPLOAD_FOLDER = "shared/uploads"

router = APIRouter()

@router.get("/", response_class=HTMLResponse)
async def upload_form():
    return """
    <form action="/upload" enctype="multipart/form-data" method="post">
        <input name="file" type="file">
        <input type="submit">
    </form>
    """

@router.post("/upload")
async def upload_file(file: UploadFile = File(...)):
    # Validate filename
    if not file.filename or file.filename.strip() == "":
        raise HTTPException(status_code=400, detail="No file selected or filename is empty")
    
    os.makedirs(UPLOAD_FOLDER, exist_ok=True)
    file_path = os.path.join(UPLOAD_FOLDER, file.filename)

    with open(file_path, "wb") as buffer:
        shutil.copyfileobj(file.file, buffer)

    # Trigger processor (dummy call here)
    try:
        requests.post("http://processor:5000/process", json={"filename": file.filename})
    except:
        print("Processor service not reachable.")

    return {"message": "Upload successful", "filename": file.filename}

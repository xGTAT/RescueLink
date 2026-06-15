import os
from fastapi import FastAPI, HTTPException, status
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from pydantic_settings import BaseSettings, SettingsConfigDict
from supabase import create_client, Client

# --- UPDATED FOR PYDANTIC V2 ---
class Settings(BaseSettings):
    SUPABASE_URL: str
    SUPABASE_KEY: str

    model_config = SettingsConfigDict(env_file=".env")

try:
    settings = Settings()
except Exception as e:
    print("CRITICAL: Error loading .env file. Ensure SUPABASE_URL and SUPABASE_KEY are set.")
    raise e

app = FastAPI(title="RescueLink Telematics API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False, 
    allow_methods=["*"],
    allow_headers=["*"],
)

# Initialize Supabase Client
supabase: Client = create_client(settings.SUPABASE_URL, settings.SUPABASE_KEY)

class IncidentPayload(BaseModel):
    device_id: str
    latitude: float
    longitude: float
    status: str

@app.post("/api/incidents", status_code=status.HTTP_201_CREATED)
async def report_incident(payload: IncidentPayload):
    try:
        data = {
            "device_id": payload.device_id,
            "latitude": payload.latitude,
            "longitude": payload.longitude,
            "status": payload.status
        }
        
        response = supabase.table("incidents").insert(data).execute()
        return {"message": "Incident logged successfully", "data": response.data}
        
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Failed to log incident: {str(e)}"
        )

@app.get("/api/incidents")
async def get_incidents(limit: int = 50):
    try:
        response = supabase.table("incidents") \
            .select("*") \
            .order("created_at", desc=True) \
            .limit(limit) \
            .execute()
            
        return response.data
    except Exception as e:
        raise HTTPException(
            status_code=status.HTTP_500_INTERNAL_SERVER_ERROR,
            detail=f"Database retrieval failed: {str(e)}"
        )
import os
import base64
import time
from typing import Optional, List, Dict, Any
from fastapi import FastAPI, HTTPException, Query, Body, Path
from pydantic import BaseModel, EmailStr
from google.oauth2.credentials import Credentials
from google_auth_oauthlib.flow import InstalledAppFlow
from googleapiclient.discovery import build
from googleapiclient.errors import HttpError
from email.mime.text import MIMEText

# FastAPI app
app = FastAPI(
    title="Gmail API Microservice",
    description="A microservice for interacting with Gmail API",
    version="1.0.0"
)

# Pydantic models
class EmailMessage(BaseModel):
    to: EmailStr
    subject: str
    body: str

class LabelCreate(BaseModel):
    name: str
    label_list_visibility: str = "labelShow"
    message_list_visibility: str = "show"

class LabelUpdate(BaseModel):
    name: Optional[str] = None
    label_list_visibility: Optional[str] = None
    message_list_visibility: Optional[str] = None

class GmailManager:
    def __init__(self, credentials_path='runtime-deps/credentials.json', token_path='runtime-deps/token.json'):
        """
        Initialize Gmail API service with OAuth 2.0 authentication
        
        :param credentials_path: Path to OAuth 2.0 credentials file
        :param token_path: Path to store access token
        """
        self.SCOPES = [
            'https://www.googleapis.com/auth/gmail.readonly',  # Read emails
            'https://www.googleapis.com/auth/gmail.send',      # Send emails
            'https://www.googleapis.com/auth/gmail.modify',    # Modify emails
            'https://www.googleapis.com/auth/gmail.labels'     # Access to labels
        ]
        
        self.credentials_path = credentials_path
        self.token_path = token_path
        self.service = self._get_gmail_service()

    def _get_gmail_service(self):
        """
        Authenticate and create Gmail API service
        
        :return: Gmail API service object
        """
        creds = None
        
        # Check if token exists
        if os.path.exists(self.token_path):
            creds = Credentials.from_authorized_user_file(self.token_path, self.SCOPES)
        
        # If no valid credentials, initiate OAuth flow
        if not creds or not creds.valid:
            flow = InstalledAppFlow.from_client_secrets_file(
                self.credentials_path, self.SCOPES)
            creds = flow.run_local_server(port=0)
            
            # Save credentials for next run
            with open(self.token_path, 'w') as token:
                token.write(creds.to_json())
        
        return build('gmail', 'v1', credentials=creds)
    
    """ USER PROFILE """
    
    def get_profile(self):
        """
        Get the user's Gmail profile
        
        :return: Gmail profile information
        """
        try:
            profile = self.service.users().getProfile(userId='me').execute()
            return profile
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    """ HISTORY """

    def get_history(self, start_history_id=None, max_results=100):
        """
        Get the history of changes to the user's mailbox.
        
        :param start_history_id: Starting point for fetching history (optional)
        :param max_results: Maximum number of history records to return
        :return: List of history records
        """
        try:
            # If no start_history_id is provided, get the current one
            if not start_history_id:
                profile = self.get_profile()
                start_history_id = profile.get('historyId', 1)
                # Wait a moment to ensure there might be some history to fetch
                time.sleep(1)
            
            params = {
                'userId': 'me',
                'startHistoryId': start_history_id,
                'maxResults': max_results,
                'historyTypes': ['messageAdded', 'messageDeleted', 'labelAdded', 'labelRemoved']
            }
            
            history = self.service.users().history().list(**params).execute()
            
            return {
                'history_id': start_history_id,
                'history_records': history.get('history', []),
                'next_page_token': history.get('nextPageToken', None)
            }
            
        except HttpError as e:
            print(f"An error occurred: {e}")
            if e.resp.status == 404:
                print("History ID not found. The ID might be too old.")
            return {'error': str(e)}

    """ LABELS """

    def list_labels(self):
        """
        List all labels in the user's mailbox
        
        :return: List of labels
        """
        try:
            results = self.service.users().labels().list(userId='me').execute()
            labels = results.get('labels', [])
            return labels
        except HttpError as e:
            print(f"An error occurred: {e}")
            return []

    def get_label(self, label_id):
        """
        Get details for a specific label
        
        :param label_id: ID of the label to retrieve
        :return: Label details
        """
        try:
            label = self.service.users().labels().get(userId='me', id=label_id).execute()
            return label
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def create_label(self, name, label_list_visibility='labelShow', message_list_visibility='show'):
        """
        Create a new label
        
        :param name: Name of the label
        :param label_list_visibility: Visibility in the label list (labelShow/labelHide/labelShowIfUnread)
        :param message_list_visibility: Visibility in the message list (show/hide)
        :return: Created label details
        """
        try:
            label_object = {
                'name': name,
                'labelListVisibility': label_list_visibility,
                'messageListVisibility': message_list_visibility
            }
            
            created_label = self.service.users().labels().create(
                userId='me', 
                body=label_object
            ).execute()
            
            return created_label
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def update_label(self, label_id, name=None, label_list_visibility=None, message_list_visibility=None):
        """
        Update an existing label
        
        :param label_id: ID of the label to update
        :param name: New name for the label (optional)
        :param label_list_visibility: New visibility in label list (optional)
        :param message_list_visibility: New visibility in message list (optional)
        :return: Updated label details
        """
        try:
            # Get the current label first
            current_label = self.get_label(label_id)
            if 'error' in current_label:
                return current_label
            
            # Update only the provided fields
            if name:
                current_label['name'] = name
            if label_list_visibility:
                current_label['labelListVisibility'] = label_list_visibility
            if message_list_visibility:
                current_label['messageListVisibility'] = message_list_visibility
            
            updated_label = self.service.users().labels().update(
                userId='me', 
                id=label_id, 
                body=current_label
            ).execute()
            
            return updated_label
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def delete_label(self, label_id):
        """
        Delete a label
        
        :param label_id: ID of the label to delete
        :return: Success status
        """
        try:
            self.service.users().labels().delete(userId='me', id=label_id).execute()
            return {'success': True, 'message': f'Label {label_id} deleted successfully'}
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'success': False, 'error': str(e)}

    """ MESSAGES """

    def list_messages(self, query='', max_results=10):
        """
        List messages from Gmail inbox
        
        :param query: Optional search query to filter messages
        :param max_results: Maximum number of messages to retrieve
        :return: List of message details
        """
        try:
            results = self.service.users().messages().list(
                userId='me', 
                q=query, 
                maxResults=max_results
            ).execute()
            
            messages = results.get('messages', [])
            
            detailed_messages = []
            for msg in messages:
                txt = self.service.users().messages().get(
                    userId='me', 
                    id=msg['id']
                ).execute()
                
                try:
                    payload = txt['payload']
                    headers = payload['headers']
                    
                    # Extract subject and sender
                    subject = next((h['value'] for h in headers if h['name'] == 'Subject'), 'No Subject')
                    sender = next((h['value'] for h in headers if h['name'] == 'From'), 'Unknown Sender')
                    
                    # Decode message body
                    if 'parts' in payload:
                        body = payload['parts'][0]['body'].get('data', '')
                    else:
                        body = payload.get('body', {}).get('data', '')
                    
                    body = base64.urlsafe_b64decode(body + '===').decode('utf-8') if body else 'No body'
                    
                    detailed_messages.append({
                        'id': msg['id'],
                        'subject': subject,
                        'sender': sender,
                        'snippet': body[:100] + '...' if len(body) > 100 else body
                    })
                except Exception as detail_error:
                    print(f"Error processing message details: {detail_error}")
            
            return detailed_messages
        
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def send_message(self, to, subject, body):
        """
        Send an email using Gmail API
        
        :param to: Recipient email address
        :param subject: Email subject
        :param body: Email body text
        :return: Sent message ID or None
        """
        try:
            message = MIMEText(body)
            message['to'] = to
            message['subject'] = subject
            raw_message = base64.urlsafe_b64encode(message.as_bytes()).decode('utf-8')
            
            send_message = self.service.users().messages().send(
                userId='me', 
                body={'raw': raw_message}
            ).execute()
            
            print(f"Message sent. Message ID: {send_message['id']}")
            return send_message['id']
        
        except HttpError as e:
            print(f"An error occurred: {e}")
            return {'error': str(e)}

    def trash_message(self, message_id):
        """
        Move a message to trash.
        
        :param message_id: ID of the message to trash
        :return: Trashed message resource or error
        """
        try:
            trashed_message = self.service.users().messages().trash(userId='me', id=message_id).execute()
            return trashed_message
        except HttpError as e:
            print(f"An error occurred while trashing message {message_id}: {e}")
            return {'error': str(e), 'message_id': message_id}

# Initialize Gmail Manager as a global instance
gmail_manager = GmailManager()

# Health check endpoint
@app.get("/health")
def health_check():
    return {"status": "healthy", "service": "Gmail API Microservice"}

# Profile endpoints
@app.get("/profile", tags=["Profile"])
def get_profile():
    """Get the user's Gmail profile information"""
    profile = gmail_manager.get_profile()
    if 'error' in profile:
        raise HTTPException(status_code=500, detail=profile['error'])
    return profile

# Label endpoints
@app.get("/labels", tags=["Labels"])
def list_labels():
    """List all labels in the user's mailbox"""
    labels = gmail_manager.list_labels()
    return {"labels": labels}

@app.get("/labels/{label_id}", tags=["Labels"])
def get_label(label_id: str = Path(..., description="ID of the label to retrieve")):
    """Get details for a specific label"""
    label = gmail_manager.get_label(label_id)
    if 'error' in label:
        raise HTTPException(status_code=404, detail=f"Label {label_id} not found")
    return label

@app.post("/labels", tags=["Labels"])
def create_label(label: LabelCreate):
    """Create a new label"""
    result = gmail_manager.create_label(
        name=label.name,
        label_list_visibility=label.label_list_visibility,
        message_list_visibility=label.message_list_visibility
    )
    if 'error' in result:
        raise HTTPException(status_code=500, detail=result['error'])
    return result

@app.put("/labels/{label_id}", tags=["Labels"])
def update_label(
    label_id: str = Path(..., description="ID of the label to update"),
    label: LabelUpdate = Body(...)
):
    """Update an existing label"""
    result = gmail_manager.update_label(
        label_id=label_id,
        name=label.name,
        label_list_visibility=label.label_list_visibility,
        message_list_visibility=label.message_list_visibility
    )
    if 'error' in result:
        raise HTTPException(status_code=404, detail=f"Label {label_id} not found")
    return result

@app.delete("/labels/{label_id}", tags=["Labels"])
def delete_label(label_id: str = Path(..., description="ID of the label to delete")):
    """Delete a label"""
    result = gmail_manager.delete_label(label_id)
    if not result['success']:
        raise HTTPException(status_code=404, detail=f"Label {label_id} not found")
    return result

# Message endpoints
@app.get("/messages", tags=["Messages"])
def list_messages(
    query: str = Query("", description="Optional search query to filter messages"),
    max_results: int = Query(10, description="Maximum number of messages to retrieve")
):
    """List messages from Gmail inbox with optional search filter"""
    messages = gmail_manager.list_messages(query=query, max_results=max_results)
    return {"messages": messages}

@app.post("/messages", tags=["Messages"])
def send_message(message: EmailMessage):
    """Send an email"""
    message_id = gmail_manager.send_message(
        to=message.to,
        subject=message.subject,
        body=message.body
    )
    if not message_id:
        raise HTTPException(status_code=500, detail="Failed to send message")
    return {"message_id": message_id, "status": "sent"}

@app.delete("/messages/{message_id}", tags=["Messages"])
def trash_message_endpoint(message_id: str = Path(..., description="ID of the message to move to trash")):
    """
    Move a specific message to trash.
    """
    result = gmail_manager.trash_message(message_id)
    if 'error' in result:
        # Consider appropriate HTTP status code for error, e.g., 404 if not found, 500 for general API error
        # For simplicity here, let's assume HttpError from client might lead to a 500 or specific mapping.
        # If the service method already prints, FastAPI might just return its dict as JSON.
        # For a more robust API, you'd map these errors to HTTPExceptions.
        # For now, letting FastAPI return the dict which includes an 'error' key.
        # If HttpError was a 404 (e.g. message not found), that info is in the error string.
        # A real app might raise HTTPException(status_code=e.resp.status, detail=str(e))
        # For now, we return the dict from the manager which may include an error.
        # If a 404 happened in the client, it could be part of the error string.
        # The test will need to check the response body for success or inspect the error details.
        # A simple way to indicate failure for the client is to use a 500 if an error key is present.
        # However, the prompt is to not change existing functionality unless required for the tests.
        # The existing error handling returns a dict with an 'error' key.
        # We will follow that pattern.
        # If not found, Google API typically returns 404, which HttpError captures.
        # The gmail_manager.trash_message returns a dict {'error': str(e)}.
        # We should check this and potentially return a different status code.
        # For now, let's return 404 if "HttpError 404" is in the error string.
        if isinstance(result.get('error'), str) and "<HttpError 404" in result['error']:
             raise HTTPException(status_code=404, detail=f"Message with ID {message_id} not found or already trashed.")
        if 'error' in result: # other errors
            raise HTTPException(status_code=500, detail=result['error'])

    return {"success": True, "message_id": message_id, "status": "trashed"}

# History endpoints
@app.get("/history", tags=["History"])
def get_history(
    start_history_id: Optional[str] = Query(None, description="Starting point for fetching history"),
    max_results: int = Query(100, description="Maximum number of history records to return")
):
    """Get the history of changes to the user's mailbox"""
    history_data = gmail_manager.get_history(
        start_history_id=start_history_id,
        max_results=max_results
    )
    if 'error' in history_data:
        raise HTTPException(status_code=500, detail=history_data['error'])
    return history_data

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)

import os
import random
import string
import hashlib
import time
from uuid import uuid4
import shutil
import subprocess
import json
import threading

from google.oauth2.service_account import Credentials
from googleapiclient.discovery import build
from googleapiclient.http import MediaFileUpload
import dropbox


LOCAL_INIT = os.getenv('LOCAL_INIT', '/tmp/local_init')
LOCAL_TARGET = os.getenv('LOCAL_TARGET', '/tmp/local_target')
GOOGLE_INIT = os.getenv('LOCAL_GOOGLE_INIT', '/tmp/google_init')
DROPBOX_INIT = os.getenv('LOCAL_DROPBOX_INIT', '/tmp/dropbox_init')
GOOGLE_CRED_FILE = os.getenv('GOOGLE_CREDENTIALS_FILE', 'syncharbor-455414-d7e1e212daef.json')
DROPBOX_TOKEN = os.getenv('DROPBOX_TOKEN', 'sl.u.AFwajatf98-4dA7XRKJfJ56NFb-du_nBLC9AMvDnZmTsp0Jfmxe2O3XkEAgsLOL3InFXtr-LtYeYIT5XzMziSV05lt846_Ebx5RB02I57kzNR5sHvNreimzH9OG2DUeNteP_GC6M94b5SGn5qSuYTl9XQoUCra9wR6Q-W4cA9SDyznmwT19rpEnlv0fLzRJ4cLkuGXynBB8OqtXXEigbeobIsxqTpa5degiKzFi8sqb6Did8PsqyR0HiP6hoSM3yHuqyaDBo7OcGcIYcR2VIKVa4FzgGlQy-O48LeKrVNrsj3doeX1NMZrd4E6ssX2fsUB46SqravZ6miqfcOZTWPeoTGZx40w-Ip5vqemCjIfAbQ8qwqHRCHAtQUfmwvvYNC_r478XRBpljwsusC7vAq28xoItPhB3dA47hOvMiA5uuqYjoZvLV0kNWNukAAHd4nQIUNBvYZYx_-L_yjSE4wZqLUzQFx-Up_0vldzPNKiDBn0ozoC8cMryKx9YBUiEbhCOa7kOTUHLtPWWt00uFqzu305pYNdme-Laeb5mTeR2pO8t7jYeHwy4xCrim7abdvxrF5NMeCVgdwYev6pGAXxAbuir4LWcP0xGWTuvj-44mR6eXiS8BNVys8qiQrGNOnds0s3za76R_xVdMpq3vM7_qCC6Xv7FU0vMYKz3q60Cz7XQQwJ1h5DPMD3RHRN1Nomvzu-vYMD_PUp3WN6ClN8KAmnA8lP6PMNzGnhQttf94l5b_sha7T8c8Y2NqOLKChID00gVgXLGhjAgVZGaIPN5UGA6I1XE1Y2urp8sDr6opvC4l6fXtVEpZFoDYO7yAAdmNYYh6pjf8TUYR2mN4Ohe1IFrv8m-_N1Z5mtTvYI3kxyQWQRHpy-uLg9DEhuWnpkoRboMHvym8EBHdBwGWbDPQAKWuGFAqZyzZlOK5UZUw-GNnLdsiqrPX22QfoMIGyYdrnOp-yy3PvtNsM_Tk0Tb9P2j_MXoX0dD-5McRB4Xwq1Iy41msqbep1yziR01Oj95gQqde0yldQtgfSFRECuDXovUkMlE5R3QxalQ1gtPUsTYSs9ciWHRxQpNBwK73g0AKwpj9HvzGCXl1F_JkxP48C0lWEeDwSxTrD1meqDvDsqqlY2cQK3-6R_Mt9lJ9Vzt0O2NtqQVmdyVEOFcU19h3hVJTTs7a9jPhXUnNSov2wvJItqZlj8M83bWLPrgWGqjjDzonwd0Ud8JjRd1vEZ_FOsRxZ-EYH1QkZ5uvYSxJpfm4Cf8f58Wq6UNKTTsDgSft0__LvOjGqOihRQLDS2Yv7H2pOOHw3LUAv6JKt8grM3CxK6m9E0gCxS6a4pVeYF-vgmIQtoGsTcJ44bMqVGIbpAHRXxzn6C_Wkj061QRXIpgtkbTVCFDFfIOTh-lA0Kitm-EkqRZvinVaT3Ze3yCg')
GOOGLE_ROOT_ID = os.getenv('GOOGLE_ROOT_ID', '1rW7MLPtdwHsSOd4yBmf6hIbctiDeQikA')
DROPBOX_ROOT = os.getenv('DROPBOX_ROOT', '/demo')
CONFIG_FILE = os.getenv('SYNC_CONFIG', 'test-conf')
INITIAL_FLAG = 'config-file-initial'
BACKGROUND_FLAG = 'config-file-daemon'
SYNC_CMD = os.getenv('SYNC_CMD', 'syncharbor')

INIT_CMD = ['./build/bin/syncharbor', 'config-file-initial', 'test-conf']
DAEMON_CMD = ['./build/bin/syncharbor', 'config-file-daemon', 'test-conf']

POLL_INTERVAL = 1
TIMEOUT = 60


def make_random_tree(root: str, depth: int, max_entries: int):
    os.makedirs(root, exist_ok=True)
    if depth == 0:
        return
    for _ in range(random.randint(1, max_entries)):
        name = ''.join(random.choices(string.ascii_letters + string.digits, k=8))
        path = os.path.join(root, name)
        if random.random() < 0.5:
            with open(path + '.bin', 'wb') as f:
                f.write(os.urandom(random.randint(128, 1024)))
        else:
            make_random_tree(path, depth - 1, max_entries)


def init_google():
    creds = Credentials.from_service_account_file(
        GOOGLE_CRED_FILE,
        scopes=['https://www.googleapis.com/auth/drive']
    )
    return build('drive', 'v3', credentials=creds)

def init_dropbox():
    if not DROPBOX_TOKEN:
        raise ValueError('DROPBOX_TOKEN не задан')
    return dropbox.Dropbox(DROPBOX_TOKEN)


def upload_folder_google(drive, local_path, parent_id):
    for entry in os.listdir(local_path):
        full = os.path.join(local_path, entry)
        meta = {'name': entry, 'parents': [parent_id]}
        if os.path.isdir(full):
            folder = drive.files().create(
                body={**meta, 'mimeType': 'application/vnd.google-apps.folder'}
            ).execute()
            upload_folder_google(drive, full, folder['id'])
        else:
            media = MediaFileUpload(full)
            drive.files().create(body=meta, media_body=media).execute()


def upload_tree_dropbox(dbx, local_path, dropbox_path):
    for entry in os.listdir(local_path):
        full = os.path.join(local_path, entry)
        dest = dropbox_path.rstrip('/') + '/' + entry
        if os.path.isdir(full):
            try:
                dbx.files_create_folder_v2(dest)
            except Exception:
                pass
            upload_tree_dropbox(dbx, full, dest)
        else:
            with open(full, 'rb') as f:
                dbx.files_upload(f.read(), dest, mode=dropbox.files.WriteMode.overwrite)


def exists_local(root: str, name: str) -> bool:
    return os.path.exists(os.path.join(root, name))

def exists_google(drive, parent_id: str, name: str) -> bool:
    res = drive.files().list(q=f"'{parent_id}' in parents and name='{name}' and trashed=false").execute()
    return bool(res.get('files'))

def exists_dropbox(dbx, path: str, name: str) -> bool:
    try:
        res = dbx.files_list_folder(path)
        return any(e.name == name for e in res.entries)
    except Exception:
        return False


def create_file_google(drive, parent_id: str) -> str:
    name = 'op_' + uuid4().hex + '.bin'
    tmp = f'/tmp/{name}'
    with open(tmp, 'wb') as f:
        f.write(os.urandom(256))
    drive.files().create(
        body={'name': name, 'parents': [parent_id]},
        media_body=MediaFileUpload(tmp)
    ).execute()
    return name

def create_file_dropbox(dbx, path: str) -> str:
    name = 'op_' + uuid4().hex + '.bin'
    dbx.files_upload(os.urandom(256), f'{path.rstrip('/')}/{name}', mode=dropbox.files.WriteMode.overwrite)
    return name

def delete_file_google(drive, parent_id: str) -> str:
    res = drive.files().list(q=f"'{parent_id}' in parents and trashed=false and mimeType!='application/vnd.google-apps.folder'").execute().get('files', [])
    target = random.choice(res)
    drive.files().delete(fileId=target['id']).execute()
    return target['name']

def delete_file_dropbox(dbx, path: str) -> str:
    files = [e for e in dbx.files_list_folder(path).entries if not isinstance(e, dropbox.files.FolderMetadata)]
    target = random.choice(files)
    dbx.files_delete_v2(f"{path.rstrip('/')}/{target.name}")
    return target.name

def rename_file_google(drive, parent_id: str) -> (str, str):
    res = drive.files().list(q=f"'{parent_id}' in parents and trashed=false and mimeType!='application/vnd.google-apps.folder'").execute().get('files', [])
    target = random.choice(res)
    new_name = 'ren_' + uuid4().hex + os.path.splitext(target['name'])[1]
    drive.files().update(fileId=target['id'], body={'name': new_name}).execute()
    return target['name'], new_name

def rename_file_dropbox(dbx, path: str) -> (str, str):
    files = [e for e in dbx.files_list_folder(path).entries if not isinstance(e, dropbox.files.FolderMetadata)]
    target = random.choice(files)
    old = target.name
    new = 'ren_' + uuid4().hex + os.path.splitext(old)[1]
    dbx.files_move_v2(f"{path.rstrip('/')}/{old}", f"{path.rstrip('/')}/{new}")
    return old, new

def update_file_google(drive, parent_id: str) -> str:
    res = drive.files().list(q=f"'{parent_id}' in parents and trashed=false and mimeType!='application/vnd.google-apps.folder'").execute().get('files', [])
    target = random.choice(res)
    tmp = f"/tmp/upd_{target['name']}"
    with open(tmp, 'wb') as f:
        f.write(os.urandom(128))
    drive.files().update(fileId=target['id'], media_body=MediaFileUpload(tmp)).execute()
    return target['name']

def update_file_dropbox(dbx, path: str) -> str:
    files = [e for e in dbx.files_list_folder(path).entries if not isinstance(e, dropbox.files.FolderMetadata)]
    target = random.choice(files)
    dbx.files_upload(os.urandom(128), f"{path.rstrip('/')}/{target.name}", mode=dropbox.files.WriteMode.overwrite)
    return target.name

def move_file_google(drive, parent_id: str) -> str:
    all_folders = drive.files().list(q=f"'{parent_id}' in parents and mimeType='application/vnd.google-apps.folder' and trashed=false").execute().get('files', [])
    files = drive.files().list(q=f"'{parent_id}' in parents and mimeType!='application/vnd.google-apps.folder' and trashed=false").execute().get('files', [])
    target = random.choice(files)
    dest_folder = random.choice(all_folders)
    drive.files().update(
        fileId=target['id'],
        addParents=dest_folder['id'],
        removeParents=parent_id
    ).execute()
    return target['name']

def move_file_dropbox(dbx, path: str) -> str:
    entries = dbx.files_list_folder(path).entries
    folders = [e for e in entries if isinstance(e, dropbox.files.FolderMetadata)]
    files = [e for e in entries if not isinstance(e, dropbox.files.FolderMetadata)]
    target = random.choice(files)
    if not folders:
        new_folder = f"{path.rstrip('/')}/folder_{uuid4().hex}"
        dbx.files_create_folder_v2(new_folder)
        folders.append(dbx.files_get_metadata(new_folder))
    dest = random.choice(folders).path_lower
    dbx.files_move_v2(f"{path.rstrip('/')}/{target.name}", f"{dest}/{target.name}")
    return target.name


def cleanup_google(drive, parent_id: str):
    for it in drive.files().list(q=f"'{parent_id}' in parents").execute().get('files', []):
        drive.files().delete(fileId=it['id']).execute()

def cleanup_dropbox(dbx, path: str):
    for it in dbx.files_list_folder(path).entries:
        dbx.files_delete_v2(f"{path.rstrip('/')}/{it.name}")


def run_daemon():
    subprocess.run(DAEMON_CMD)


def main():
    for d in (LOCAL_INIT, GOOGLE_INIT, DROPBOX_INIT, LOCAL_TARGET):
        shutil.rmtree(d, ignore_errors=True)

    make_random_tree(LOCAL_INIT, depth=3, max_entries=5)
    make_random_tree(GOOGLE_INIT, depth=3, max_entries=5)
    make_random_tree(DROPBOX_INIT, depth=3, max_entries=5)

    drive = init_google()
    dbx = init_dropbox()

    upload_folder_google(drive, GOOGLE_INIT, GOOGLE_ROOT_ID)
    upload_tree_dropbox(dbx, DROPBOX_INIT, DROPBOX_ROOT)
    shutil.copytree(LOCAL_INIT, LOCAL_TARGET)

    print('Starting initial sync...')
    subprocess.run(INIT_CMD, check=True)
    print('Initial sync completed')

    print('Launching background sync...')
    t = threading.Thread(target=run_daemon, daemon=True)
    t.start()
    time.sleep(2)

    ops = [
        lambda: ('Create Google file', create_file_google, GOOGLE_ROOT_ID),
        lambda: ('Create Dropbox file', create_file_dropbox, DROPBOX_ROOT),
        lambda: ('Delete Google file', delete_file_google, GOOGLE_ROOT_ID),
        lambda: ('Delete Dropbox file', delete_file_dropbox, DROPBOX_ROOT),
        lambda: ('Rename Google file', rename_file_google, GOOGLE_ROOT_ID),
        lambda: ('Rename Dropbox file', rename_file_dropbox, DROPBOX_ROOT),
        lambda: ('Update Google file', update_file_google, GOOGLE_ROOT_ID),
        lambda: ('Update Dropbox file', update_file_dropbox, DROPBOX_ROOT),
        lambda: ('Move Google file', move_file_google, GOOGLE_ROOT_ID),
        lambda: ('Move Dropbox file', move_file_dropbox, DROPBOX_ROOT),
    ]
    for desc_fn in ops:
        desc, fn, loc = desc_fn()
        print(f'Operation: {desc}')
        result = fn(drive if 'Google' in desc else dbx, loc)
        names = result if isinstance(result, tuple) else (result,)
        for name in names:
            start = time.time()
            while time.time() - start < TIMEOUT:
                ok_local = exists_local(LOCAL_TARGET, name)
                ok_g = exists_google(drive, GOOGLE_ROOT_ID, name)
                ok_db = exists_dropbox(dbx, DROPBOX_ROOT, name)
                if ok_local and ok_g and ok_db:
                    print(f'  {desc}: {name} synced')
                    break
                time.sleep(POLL_INTERVAL)
            else:
                raise TimeoutError(f'{desc}: {name} did not sync in time')

    print('Cleaning up clouds...')
    cleanup_google(drive, GOOGLE_ROOT_ID)
    cleanup_dropbox(dbx, DROPBOX_ROOT)
    print('Cleanup completed')

if __name__ == '__main__':
    main()
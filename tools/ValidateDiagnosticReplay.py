"""Inspect and execute a frozen diagnostic session using only its owned fixture."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import subprocess
from ValidateCli import png16_pixels


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check_replay(archive, manifest, native):
    info = json.loads(archive.read('command_replay.json'))
    require(info == manifest['command_replay'] and type(info['schema']) is int and info['schema'] == 1 and info['available'] is True, 'frozen replay metadata')
    require(info['engine'] == native['engine'] and info['parameter_revision'] == str(native['parameter_revision']), 'replay engine/revision')
    require(info['source'] == native['owned_source_path'] and info['source_is_RAW'] is False, 'owned replay source')
    require(info['source_content_verified_at_capture'] is False and info['source_hash_enforced'] is False, 'source hash limitation')
    require(info['original_file_included'] is False and info['catalog_writes'] is False and info['source_writes'] is False, 'read-only replay contract')
    require(re.fullmatch(r'[0-9a-f]{64}', info['stored_intent_sha256']) is not None, 'stored intent digest')
    require(info['session_entry'] == 'replay_session.json' and info['cli_option'] == '--session', 'bounded session entry')
    entry = archive.getinfo('replay_session.json')
    require(0 < entry.file_size == info['session_bytes'] <= 4*1024*1024, 'session byte bound')
    payload = archive.read('replay_session.json')
    require(hashlib.sha256(payload).hexdigest() == info['session_sha256'], 'session digest')
    plan = json.loads(payload)
    require(set(plan) == {'schema','engine','commands'} and type(plan['schema']) is int and plan['schema'] == 1 and plan['engine'] == native['engine'], 'session schema/engine')
    commands = plan['commands']
    require(type(commands) is list and len(commands) == 3, 'snapshot session actions')
    require(commands[0] == {'command':'photo.import','photo':'snapshot','source':native['owned_source_path']}, 'frozen import')
    require(set(commands[1]) == {'command','commands'} and commands[1]['command'] == 'edit.batch', 'atomic replay batch')
    edits = commands[1]['commands']
    require(type(edits) is list and 1 <= len(edits) <= 9997 and edits[0] == {'command':'develop.reset'}, 'complete bounded replacement')
    require(commands[2] == {'command':'export.run','destination':'replay.png','space':'srgb'}, 'relative replay output')
    return info, payload


def execute_replay(cli, environment, reports, mode, archive, manifest, native, report_path):
    info, payload = check_replay(archive, manifest, native)
    source = Path(native['owned_source_path'])
    require(not source.is_symlink() and source.resolve() == Path(str(report_path.resolve())+'.source.png'), 'owned fixture path')
    require(native['source_unchanged'] is True and sha(source) == native['owned_source_sha256'], 'original fixture hash')
    result = {'metadata_checked':True,'CLI_executed':False,'source_hash_enforced':False}
    if cli is None:
        return result
    cli = Path(cli).resolve(strict=True)
    cli_sha = sha(cli)
    output = reports / ('diagnostic-'+mode+'-replay')
    output.mkdir(exist_ok=False)
    session = output/'replay_session.json'
    with session.open('xb') as stream:
        stream.write(payload)
    require(archive.getinfo('current_preview.png').file_size <= 4*1024*1024, 'preview byte bound')
    expected = output/'current_preview.png'
    with expected.open('xb') as stream:
        stream.write(archive.read('current_preview.png'))
    process = subprocess.run([str(cli),'--session',str(session)],cwd=output,env=environment,capture_output=True,text=True,timeout=60)
    with (output/'CLI.stdout.json').open('x') as stream: stream.write(process.stdout)
    with (output/'CLI.stderr.txt').open('x') as stream: stream.write(process.stderr)
    require(process.returncode == 0, 'normal CLI replay failed')
    replay = json.loads(process.stdout)
    require(replay['ok'] is True and replay['mode'] == 'session' and replay['engine'] == native['engine']
            and replay['source_commit'] == native['source_commit'] and replay['job_count'] == 1 and replay['completed'] == 1, 'exact replay CLI/binary')
    require(replay['session']['manifest_sha256'] == info['session_sha256'] and replay['session']['catalog_writes'] is False, 'exact ZIP session bytes')
    require(len(replay['results']) == 1 and replay['results'][0]['source'] == native['owned_source_path']
            and replay['results'][0]['adjustments'] == native['adjustments'], 'frozen replay adjustments')
    target = output/'replay.png'
    require(png16_pixels(target) == png16_pixels(expected), 'full-source replay PNG16 pixels/ICC versus frozen diagnostic')
    png_sha = sha(target)
    # Existing-output refusal is part of the normal CLI preflight, not a retry.
    refused = subprocess.run([str(cli),'--session',str(session)],cwd=output,env=environment,capture_output=True,text=True,timeout=60)
    with (output/'existing-output.stdout.txt').open('x') as stream: stream.write(refused.stdout)
    with (output/'existing-output.stderr.txt').open('x') as stream: stream.write(refused.stderr)
    require(refused.returncode != 0 and 'Destination already exists' in refused.stderr and sha(target) == png_sha,
            'existing replay output must remain unchanged')
    require(sha(source) == native['owned_source_sha256'] and sha(cli) == cli_sha and sha(session) == info['session_sha256'], 'source/CLI/session unchanged')
    result.update(CLI_executed=True,CLI_sha256=cli_sha,PNG_sha256=png_sha,full_PNG16_pixels_ICC_equal=True,existing_output_refused=True,original_unchanged=True)
    return result

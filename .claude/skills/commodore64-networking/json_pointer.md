# JSON Pointer Syntax (RFC 6901) — Meatloaf Networking Reference

Meatloaf's `j <json-pointer>` command lets you extract values from JSON response bodies using **JSON Pointer** (RFC 6901). This is a simpler alternative to jq — instead of `.name.first` you write `/name/first`. Use this after sending an HTTP request with Meatloaf to pull out specific fields from the JSON response without reading the entire body.

---

## Basics

A JSON Pointer is a string of path components separated by `/`:

| Pointer | Meaning |
|---------|---------|
| `/` | The entire root document |
| `/name` | The `name` field at the root |
| `/choices/0` | The first element of the `choices` array |
| `/choices/0/message/content` | The `content` field inside the first `choices` element |

---

## How to use it with Meatloaf

```basic
10 open 1,8,2,"http://example.com/data.json"
20 print#1,"m get"
30 print#1,"s"
40 print#1,"j /some/path"
50 get#1,a$:if st and 64 then 70 : rem EOI = done
60 print chr$(asc(a$));:goto 50
70 close 1
```

The `j` command extracts the value at the given pointer and puts it in the read buffer. Then you read it with `GET#`.

---

## Examples

Given this JSON response body:

```json
{
  "id": {
    "bioguide": "E000295",
    "thomas": "02283"
  },
  "name": {
    "first": "Joni",
    "last": "Ernst"
  },
  "terms": [
    {
      "type": "sen",
      "start": "2015-01-06",
      "state": "IA"
    }
  ]
}
```

### Root field
```
print#1,"j /name"
```
Reads: `{"first":"Joni","last":"Ernst"}`

### Nested field
```
print#1,"j /name/first"
```
Reads: `Joni`

### Array element
```
print#1,"j /terms/0"
```
Reads: `{"type":"sen","start":"2015-01-06","state":"IA"}`

### Array element, nested field
```
print#1,"j /terms/0/start"
```
Reads: `2015-01-06`

### Non-existent path
```
print#1,"j /terms/1/start"
```
The value doesn't exist. The firmware sets the response status to
`-99` and queues a short error string (e.g. `JSON pointer not found`
or `JSON parse error`) for read-back. Read until EOI to get the
message, or read `status` first to get the `-99` code.

---

## Serialized Types

| JSON type | Output | Example |
|-----------|--------|---------|
| String | Raw bytes (no quotes) | `Joni` |
| Number | Decimal text | `42` or `3.14` |
| Boolean | `TRUE` or `FALSE` | `TRUE` |
| Null | `NULL` | `NULL` |
| Object | Unformatted JSON text | `{"first":"Joni"}` |
| Array | Unformatted JSON text | `[1,2,3]` |

---

## Reading the Extracted Value

```basic
60 print#1,"j /choices/0/message/content"
70 get#1,a$:if st and 64 then 90 : rem EOI = done
80 print chr$(asc(a$));:goto 70
90 rem done reading json value
```

Check for errors:

```basic
10 print#1,"status"     : rem position on status code
20 get#1,a$:b$=b$+a$:if st=0 then 20
30 if val(b$)=-99 then print"json error":close 1:end
40 print#1,"j /nonexistent/path"
50 get#1,a$:if st and 64 then 70   : rem EOI = done reading message
60 print chr$(asc(a$));:goto 50   : rem print the error message
70 close 1
```

A `-99` status means either a JSON parse failure (the response body
wasn't valid JSON) or a pointer that didn't match anything in the
captured body. The error string after `status` is one of
`JSON parse error`, `JSON pointer not found`, or
`JSON serialize error`.

---

## Escaping Special Characters

When field names contain `/` or `~`, RFC 6901 uses these escape sequences:

| Character | Escape |
|-----------|--------|
| `~` | `~0` |
| `/` | `~1` |

Example: field name `a/b` → pointer `/a~1b`. This is rarely needed in practice.

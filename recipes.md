# Recipe Formats


## Simple Recipes

Basic format for a recipe:

```
     {
       "files": {
         "obj": "/path/to/digital-object-file.tiff"
       }, 
       "uuid": "image-1", 
       "update": "true", 
       "label": "Test Image 1", 
       "import": "image", 
       "metadata": {
           "mods": "/home/lmc/image_data/MODS.xml"
       }
     }

```

The recipe elements are: 

* `files` dictionary should files for an Islandora object. Available keys include `obj`, `tn`, or `exif`, and the values can be absolute paths or URLs. 
* `uuid` should be the UUID corresponding to the simple object, but that's not enforced. Could be any string that's not already assigned as an object PID.  
* `metadata` dictionary should include  metadata files for an Islandora object. Available keys include, `mods`, `dc`, `marcxml`. MARC data will be mapped to MODS.
* `update` indicates that a new object must be created when set to `false`. If update is true, importing the recipe for an object whose PID already exists in Islandora will update the existing object (rather than failing). 
* `label`: should be a human readable identifier for the content. 
* `import` indicates the import type. Valid values include `pdf`,`image`,`audio` for basic recipes, and `book`, `page`, and `collection` for more complicated recipes (documented below).  

## Book Recipes

The recipe for a single book:

```
{
  "recipe": {
    "uuid": "ff4806af-5526-53f9-8ce3-afe82e04f03b", 
    "update": "true", 
    "label": "Islamicate text, green velvet, gold embossed.", 
    "import": "book", 
    "metadata": {
      "mods": "/home/lmc/bl5_data/metadata/BL5_McGhee_Arabic-manuscript-1-green.xml"
    },

    "pages": [
         ... zero or more book page recipes... 
    ]
}

```

Recipe elements are defined as above, with the addition of the `pages` key, which must be a list of Page object recipes. A `files` list may be provided, for setting the `TN` datastream, etc.  but this is unusual for book recipes.  

### Page Recipes 

Page objects recipes must be included inline in a book recipe. The recipe format for a book page is:

```
{
  "md5": "0d7d462c0a9dbb00252592d2ec59bda1", 
  "uuid": "bd20e06f-26e8-5c12-a915-b56917eed363", 
  "file": "/path/to/file.tiff", 
  "label": "Image 1"
} 
```

With recipe elements defines as above. 

## Collection Recipes

The reciepe for a collection of simple objects are books. Recipe elemetns are defined as above with the addition of the `members` key, which must be a list of inline Simple or Book object recipes. 

{
  "recipe": {
    "uuid": "test", 
    "update": "true", 
    "label": "My Test Collection Name", 
    "import": "collection", 
    "metadata": {
      "mods": "/home/lmc/bl5_data/metadata/BL5_McGhee_Arabic-manuscript-1-green.xml"
    },
    "members": [
	...zero or more simple or book objects...
    ] 
  }
}
```

Collection recipes CANNOT include Collections as members at this time. A `files` list may be provided, for setting the `TN` datastream, etc, but is not required. 